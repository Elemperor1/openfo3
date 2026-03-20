#include "maindialog.hpp"

#include <QByteArray>
#include <QBuffer>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>
#include <QTextStream>
#include <QTime>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/qtconfigpath.hpp>
#include <components/files/qtconversion.hpp>
#include <components/misc/helpviewer.hpp>
#include <components/misc/utf8qtextstream.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include "datafilespage.hpp"
#include "graphicspage.hpp"
#include "importpage.hpp"
#include "settingspage.hpp"

namespace
{
    constexpr const char* toolBarStyle = "QToolBar { border: 0px; } QToolButton { min-width: 70px }";
    constexpr const char* modeSettingsFile = "openfo3-launcher.ini";
    constexpr const char* modeKey = "gameMode";
    constexpr const char* windowWidthKey = "MainWindow/width";
    constexpr const char* windowHeightKey = "MainWindow/height";
    constexpr const char* windowPosXKey = "MainWindow/posx";
    constexpr const char* windowPosYKey = "MainWindow/posy";

    bool containsInsensitive(const QStringList& list, const QString& value)
    {
        return list.contains(value, Qt::CaseInsensitive);
    }

    void appendUnique(QStringList& list, const QString& value)
    {
        if (!value.isEmpty() && !containsInsensitive(list, value))
            list.append(value);
    }

    QString normalizePathForComparison(const QString& path)
    {
        const QFileInfo info(path);
        if (info.exists())
        {
            const QString canonical = info.canonicalFilePath();
            if (!canonical.isEmpty())
                return canonical;
            return info.absoluteFilePath();
        }
        return QDir::cleanPath(info.absoluteFilePath());
    }

    bool hasFallout3Master(const QString& dataDir)
    {
        return QFileInfo::exists(QDir(dataDir).filePath(QStringLiteral("Fallout3.esm")));
    }

    bool sameContentSelection(const QStringList& selectedFiles, const QList<Config::SettingValue>& detectedFiles)
    {
        if (selectedFiles.size() != detectedFiles.size())
            return false;

        for (qsizetype i = 0; i < selectedFiles.size(); ++i)
        {
            if (selectedFiles.at(i).compare(detectedFiles.at(i).value, Qt::CaseInsensitive) != 0)
                return false;
        }

        return true;
    }

    QStringList fallout3DataDirCandidates()
    {
        QStringList candidates;
        const auto addCandidate = [&](const QString& path) {
            const QString normalized = normalizePathForComparison(path);
            appendUnique(candidates, normalized);
        };

        const auto addVariants = [&](const QString& rootPath) {
            if (rootPath.isEmpty())
                return;

            const QDir root(rootPath);
            addCandidate(root.absolutePath());
            addCandidate(root.filePath(QStringLiteral("Data")));
            addCandidate(root.filePath(QStringLiteral("game_files/Data")));
        };

        const QStringList anchors = { QDir::currentPath(), QCoreApplication::applicationDirPath() };
        for (const auto& anchor : anchors)
        {
            QDir root(anchor);
            for (int depth = 0; depth < 8; ++depth)
            {
                addVariants(root.absolutePath());
                if (!root.cdUp())
                    break;
            }
        }

        return candidates;
    }

    void appendArchiveIfPresent(QStringList& archives, const QString& dataDir, const QString& archiveName)
    {
        if (QFileInfo::exists(QDir(dataDir).filePath(archiveName)))
            appendUnique(archives, archiveName);
    }

    void appendDlcArchives(QStringList& archives, const QString& dataDir, const QStringList& contentFiles)
    {
        for (const auto& contentFile : contentFiles)
        {
            if (contentFile.compare(QStringLiteral("Fallout3.esm"), Qt::CaseInsensitive) == 0)
                continue;

            const QString baseName = QFileInfo(contentFile).completeBaseName();
            if (baseName.isEmpty())
                continue;

            appendArchiveIfPresent(archives, dataDir, QStringLiteral("%1 - Main.bsa").arg(baseName));
            appendArchiveIfPresent(archives, dataDir, QStringLiteral("%1 - Sounds.bsa").arg(baseName));
        }
    }

    QStringList makeFallbackArchives(const QString& dataDir, const QStringList& contentFiles)
    {
        QStringList archives;
        const QStringList baseArchives = { QStringLiteral("Fallout - Meshes.bsa"), QStringLiteral("Fallout - Textures.bsa"),
            QStringLiteral("Fallout - Voices.bsa"), QStringLiteral("Fallout - Sound.bsa"),
            QStringLiteral("Fallout - Misc.bsa"), QStringLiteral("Fallout - MenuVoices.bsa") };

        for (const auto& archive : baseArchives)
            appendArchiveIfPresent(archives, dataDir, archive);

        appendDlcArchives(archives, dataDir, contentFiles);

        return archives;
    }
}

using namespace Process;

void cfgError(const QString& title, const QString& msg)
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setText(msg);
    msgBox.exec();
}

Launcher::MainDialog::MainDialog(
    const Files::ConfigurationManager& configurationManager, const QString& resourcesPath, QWidget* parent)
    : QMainWindow(parent)
    , mCfgMgr(configurationManager)
    , mResourcesPath(resourcesPath)
    , mGameMode(loadPersistedGameMode())
    , mGameSettings(mCfgMgr)
{
    setupUi(this);

    mGameModeComboBox = new QComboBox(this);
    mGameModeComboBox->addItem(tr("OpenMW"), ContentSelectorModel::gameModeToString(ContentSelectorModel::GameMode_OpenMW));
    mGameModeComboBox->addItem(
        tr("Fallout 3"), ContentSelectorModel::gameModeToString(ContentSelectorModel::GameMode_Fallout3));
    mGameModeComboBox->setToolTip(tr("Choose which game launcher mode to use"));
    toolBar->addSeparator();
    toolBar->addWidget(mGameModeComboBox);
    connect(mGameModeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainDialog::gameModeChanged);

    mGameInvoker = new ProcessInvoker();
    mWizardInvoker = new ProcessInvoker();

    connect(mWizardInvoker->getProcess(), &QProcess::started, this, &MainDialog::wizardStarted);

    connect(mWizardInvoker->getProcess(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        &MainDialog::wizardFinished);

    buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Launch OpenMW"));
    buttonBox->button(QDialogButtonBox::Help)->setText(tr("Help"));

    buttonBox->button(QDialogButtonBox::Ok)->setMinimumWidth(160);

    // Order of buttons can be different on different setups,
    // so make sure that the Play button has a focus by default.
    buttonBox->button(QDialogButtonBox::Ok)->setFocus();

    connect(buttonBox, &QDialogButtonBox::rejected, this, &MainDialog::close);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &MainDialog::play);
    connect(buttonBox, &QDialogButtonBox::helpRequested, this, &MainDialog::help);

    // Remove what's this? button
    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    createIcons();

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);

    QLabel* logo = new QLabel(this);
    logo->setPixmap(QIcon(":/images/openmw-header.png").pixmap(QSize(294, 64)));
    toolBar->addWidget(logo);
    toolBar->setStyleSheet(toolBarStyle);

    updateModeUi();
}

Launcher::MainDialog::~MainDialog()
{
    delete mGameInvoker;
    delete mWizardInvoker;
}

bool Launcher::MainDialog::event(QEvent* event)
{
    // Apply style sheet again if style was changed
    if (event->type() == QEvent::PaletteChange)
    {
        if (toolBar != nullptr)
            toolBar->setStyleSheet(toolBarStyle);
    }

    return QMainWindow::event(event);
}

void Launcher::MainDialog::createIcons()
{
    if (!QIcon::hasThemeIcon("document-new"))
        QIcon::setThemeName("fallback");

    connect(dataAction, &QAction::triggered, this, &MainDialog::enableDataPage);
    connect(graphicsAction, &QAction::triggered, this, &MainDialog::enableGraphicsPage);
    connect(settingsAction, &QAction::triggered, this, &MainDialog::enableSettingsPage);
    connect(importAction, &QAction::triggered, this, &MainDialog::enableImportPage);
}

void Launcher::MainDialog::createPages()
{
    // Avoid creating the widgets twice
    if (pagesWidget->count() != 0)
        return;

    mDataFilesPage = new DataFilesPage(mCfgMgr, mGameSettings, mLauncherSettings, mGameMode, this);
    mGraphicsPage = new GraphicsPage(this);
    mImportPage = new ImportPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mSettingsPage = new SettingsPage(mCfgMgr, mGameSettings, this);

    // Add the pages to the stacked widget
    pagesWidget->addWidget(mDataFilesPage);
    pagesWidget->addWidget(mGraphicsPage);
    pagesWidget->addWidget(mSettingsPage);
    pagesWidget->addWidget(mImportPage);

    // Select the first page
    dataAction->setChecked(true);

    // Using Qt::QueuedConnection because signal is emitted in a subthread and slot is in the main thread
    connect(mDataFilesPage, &DataFilesPage::signalLoadedCellsChanged, mSettingsPage,
        &SettingsPage::slotLoadedCellsChanged, Qt::QueuedConnection);
}

ContentSelectorModel::GameMode Launcher::MainDialog::loadPersistedGameMode()
{
    const QSettings settings(modeSettingsPath(), QSettings::IniFormat);
    if (settings.contains(QLatin1String(modeKey)))
        return ContentSelectorModel::gameModeFromString(
            settings.value(QLatin1String(modeKey)).toString(), ContentSelectorModel::GameMode_OpenMW);

    if (!mCfgMgr.getActiveConfigPaths().empty())
        return ContentSelectorModel::GameMode_OpenMW;

    return resolveFallout3DataDir().isEmpty() ? ContentSelectorModel::GameMode_OpenMW
                                              : ContentSelectorModel::GameMode_Fallout3;
}

void Launcher::MainDialog::savePersistedGameMode() const
{
    const auto modePath = modeSettingsPath();
    const auto userConfigPath = mCfgMgr.getUserConfigPath();
    if (!exists(userConfigPath))
    {
        std::error_code ec;
        create_directories(userConfigPath, ec);
    }

    QSettings settings(modePath, QSettings::IniFormat);
    settings.setValue(QLatin1String(modeKey), ContentSelectorModel::gameModeToString(mGameMode));

    settings.setValue(QLatin1String(windowWidthKey), width());
    settings.setValue(QLatin1String(windowHeightKey), height());
    settings.setValue(QLatin1String(windowPosXKey), pos().x());
    settings.setValue(QLatin1String(windowPosYKey), pos().y());
    settings.sync();
}

QString Launcher::MainDialog::modeSettingsPath() const
{
    return Files::pathToQString(mCfgMgr.getUserConfigPath() / modeSettingsFile);
}

QString Launcher::MainDialog::launcherSettingsPath() const
{
    const QString fileName = ContentSelectorModel::isFallout3Mode(mGameMode)
        ? QStringLiteral("openfo3-launcher.cfg")
        : QString::fromLatin1(Config::LauncherSettings::sLauncherConfigFileName);
    return Files::pathToQString(mCfgMgr.getUserConfigPath() / Files::pathFromQString(fileName));
}

QString Launcher::MainDialog::currentExecutableName() const
{
    return ContentSelectorModel::isFallout3Mode(mGameMode) ? QLatin1String("openfo3") : QLatin1String("openmw");
}

QString Launcher::MainDialog::fallout3RuntimeHomePath() const
{
    const QDir userConfigDir(Files::pathToQString(mCfgMgr.getUserConfigPath()));
    return userConfigDir.filePath(QStringLiteral("openfo3-runtime-home"));
}

bool Launcher::MainDialog::prepareFallout3RuntimeHome(QString* runtimeHomePath) const
{
    const QString runtimeHome = fallout3RuntimeHomePath();
    const QDir runtimeRoot(runtimeHome);
    const QString runtimeConfigDir = runtimeRoot.filePath(QStringLiteral("Library/Preferences/openmw"));
    const QString runtimeUserDataDir = runtimeRoot.filePath(QStringLiteral("Library/Application Support/openmw"));

    QDir dir;
    if (!dir.mkpath(runtimeConfigDir) || !dir.mkpath(runtimeUserDataDir))
    {
        cfgError(tr("Error preparing OpenFO3 runtime configuration"),
            tr("<br><b>Could not create the isolated OpenFO3 runtime directories.</b><br><br>%0<br>")
                .arg(runtimeHome));
        return false;
    }

    QFile runtimeConfigFile(QDir(runtimeConfigDir).filePath(QStringLiteral("openmw.cfg")));
    if (!runtimeConfigFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        cfgError(tr("Error preparing OpenFO3 runtime configuration"),
            tr("<br><b>Could not write %0</b><br><br>%1<br>")
                .arg(runtimeConfigFile.fileName(), runtimeConfigFile.errorString()));
        return false;
    }

    QTextStream runtimeConfigStream(&runtimeConfigFile);
    Misc::ensureUtf8Encoding(runtimeConfigStream);
    runtimeConfigStream << "# Auto-generated isolated runtime config for OpenFO3.\n";
    runtimeConfigFile.close();

    const QString sourceSettingsPath
        = QDir(Files::pathToQString(mCfgMgr.getUserConfigPath())).filePath(QStringLiteral("settings.cfg"));
    const QString runtimeSettingsPath = QDir(runtimeConfigDir).filePath(QStringLiteral("settings.cfg"));
    QFile::remove(runtimeSettingsPath);
    if (QFileInfo::exists(sourceSettingsPath) && !QFile::copy(sourceSettingsPath, runtimeSettingsPath))
    {
        cfgError(tr("Error preparing OpenFO3 runtime configuration"),
            tr("<br><b>Could not copy %0 to %1</b><br><br>"
               "Please make sure you have the right permissions and try again.<br>")
                .arg(sourceSettingsPath, runtimeSettingsPath));
        return false;
    }

    if (runtimeHomePath != nullptr)
        *runtimeHomePath = runtimeHome;
    return true;
}

void Launcher::MainDialog::updateModeUi()
{
    const bool fallout3 = ContentSelectorModel::isFallout3Mode(mGameMode);
    const QString buttonText = ContentSelectorModel::isFallout3Mode(mGameMode) ? tr("Launch OpenFO3")
                                                                               : tr("Launch OpenMW");
    buttonBox->button(QDialogButtonBox::Ok)->setText(buttonText);
    setWindowTitle(fallout3 ? tr("OpenFO3 Launcher") : tr("OpenMW Launcher"));

    graphicsAction->setEnabled(!fallout3);
    settingsAction->setEnabled(!fallout3);
    importAction->setEnabled(!fallout3);
    if (fallout3 && pagesWidget->count() != 0)
        enableDataPage();

    mLoadingModeSelection = true;
    const int index = mGameModeComboBox->findData(ContentSelectorModel::gameModeToString(mGameMode));
    if (index >= 0)
        mGameModeComboBox->setCurrentIndex(index);
    mLoadingModeSelection = false;
}

QString Launcher::MainDialog::resolveFallout3DataDir() const
{
    for (const auto& candidate : fallout3DataDirCandidates())
    {
        if (hasFallout3Master(candidate))
        {
            const QString canonical = QDir(candidate).canonicalPath();
            return canonical.isEmpty() ? normalizePathForComparison(candidate) : canonical;
        }
    }

    return QString();
}

QStringList Launcher::MainDialog::collectFallout3Archives(const QString& dataDir, const QStringList& contentFiles) const
{
    return makeFallbackArchives(dataDir, contentFiles);
}

QStringList Launcher::MainDialog::collectFallout3ContentFiles(const QString& dataDir) const
{
    QStringList contentFiles;
    const QString baseFile = QDir(dataDir).filePath(QStringLiteral("Fallout3.esm"));
    if (QFileInfo::exists(baseFile))
        appendUnique(contentFiles, QStringLiteral("Fallout3.esm"));

    QFile dlcList(QDir(dataDir).filePath(QStringLiteral("DLCList.txt")));
    if (dlcList.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream stream(&dlcList);
        Misc::ensureUtf8Encoding(stream);
        while (!stream.atEnd())
        {
            const QString entry = stream.readLine().trimmed();
            if (entry.isEmpty() || entry.startsWith(QLatin1Char('#')))
                continue;
            if (QFileInfo::exists(QDir(dataDir).filePath(entry)))
                appendUnique(contentFiles, entry);
        }
    }

    return contentFiles;
}

bool Launcher::MainDialog::loadFo3GameSettings()
{
    const QString dataDir = resolveFallout3DataDir();
    if (dataDir.isEmpty())
    {
        cfgError(tr("Error detecting Fallout 3 installation"),
            tr("<br><b>Could not find the Fallout 3 Data Files location</b><br><br>"
               "The directory containing Fallout3.esm was not found."));
        return false;
    }

    QStringList contentFiles = collectFallout3ContentFiles(dataDir);
    QStringList archives = collectFallout3Archives(dataDir, contentFiles);
    QString resourcesPath = mResourcesPath;
    if (!QFileInfo(resourcesPath).isAbsolute())
        resourcesPath = QFileInfo(resourcesPath).absoluteFilePath();

    const QByteArray dataDirLog = dataDir.toUtf8();
    const QByteArray contentFilesLog = contentFiles.join(QStringLiteral(", ")).toUtf8();
    const QByteArray archivesLog = archives.join(QStringLiteral(", ")).toUtf8();
    Log(Debug::Info) << "Detected Fallout 3 data directory:" << dataDirLog.constData();
    Log(Debug::Info) << "Detected Fallout 3 content files:" << contentFilesLog.constData();
    Log(Debug::Info) << "Detected Fallout 3 fallback archives:" << archivesLog.constData();

    QString configText;
    QTextStream stream(&configText);
    Misc::ensureUtf8Encoding(stream);
    stream << "resources=" << resourcesPath << '\n';
    stream << "data=" << dataDir << '\n';
    for (const auto& archive : archives)
        stream << "fallback-archive=" << archive << '\n';
    for (const auto& content : contentFiles)
        stream << "content=" << content << '\n';
    stream << "encoding=win1252\n";
    stream.flush();

    QByteArray configBytes = configText.toUtf8();
    QBuffer buffer(&configBytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;

    QTextStream input(&buffer);
    Misc::ensureUtf8Encoding(input);
    return mGameSettings.readFile(input, dataDir, false) && mGameSettings.hasMaster();
}

void Launcher::MainDialog::applyFallout3DefaultContentSelection()
{
    if (!ContentSelectorModel::isFallout3Mode(mGameMode))
        return;

    const QString currentProfile = mLauncherSettings.getCurrentContentListName();
    if (currentProfile.isEmpty())
        return;

    const QStringList selectedFiles = mLauncherSettings.getContentListFiles(currentProfile);
    if (!sameContentSelection(selectedFiles, mGameSettings.getContentList()))
        return;

    const QStringList dataDirs = mLauncherSettings.getDataDirectoryList(currentProfile);
    QStringList archives = mLauncherSettings.getArchiveList(currentProfile);
    QString dataDir;
    for (const auto& candidate : dataDirs)
    {
        if (hasFallout3Master(candidate))
        {
            dataDir = candidate;
            break;
        }
    }

    if (dataDir.isEmpty())
        dataDir = resolveFallout3DataDir();

    if (!dataDir.isEmpty())
        archives = collectFallout3Archives(dataDir, { QStringLiteral("Fallout3.esm") });

    mLauncherSettings.setContentList(currentProfile, dataDirs, archives, { QStringLiteral("Fallout3.esm") });
}

QStringList Launcher::MainDialog::buildOpenFo3Arguments()
{
    QStringList arguments;
    QString resourcesPath = mResourcesPath.isEmpty() ? QStringLiteral(".") : mResourcesPath;
    if (!QFileInfo(resourcesPath).isAbsolute())
        resourcesPath = QFileInfo(resourcesPath).absoluteFilePath();
    arguments << QStringLiteral("--resources") << resourcesPath;

    for (const auto& dataDir : mGameSettings.getDataDirs())
        arguments << QStringLiteral("--data") << dataDir.value;

    for (const auto& archive : mGameSettings.getArchiveList())
        arguments << QStringLiteral("--fallback-archive") << archive.value;

    for (const auto& content : mGameSettings.getContentList())
        arguments << QStringLiteral("--content") << content.value;

    const QString encoding = mGameSettings.value(QStringLiteral("encoding"), { QStringLiteral("win1252") }).value;
    if (!encoding.isEmpty())
        arguments << QStringLiteral("--encoding") << encoding;

    for (const auto& fallback : mGameSettings.values(QStringLiteral("fallback")))
        arguments << QStringLiteral("--fallback") << fallback.value;

    return arguments;
}

void Launcher::MainDialog::gameModeChanged(int index)
{
    if (mLoadingModeSelection)
        return;

    const QString value = mGameModeComboBox->itemData(index).toString();
    const auto newMode = ContentSelectorModel::gameModeFromString(value, mGameMode);
    if (newMode == mGameMode)
        return;

    mGameMode = newMode;
    savePersistedGameMode();
    updateModeUi();

    if (mDataFilesPage != nullptr)
        mDataFilesPage->setGameMode(mGameMode);

    if (pagesWidget->count() != 0 && !reloadSettings())
        cfgError(tr("Error switching game mode"),
            tr("<br><b>The launcher could not reload settings for the selected mode.</b><br><br>"
               "Please check that the required game files are available."));
}

Launcher::FirstRunDialogResult Launcher::MainDialog::showFirstRunDialog()
{
    if (!setupLauncherSettings())
        return FirstRunDialogResultFailure;

    // Dialog wizard and setup will fail if the config directory does not already exist
    const auto& userConfigDir = mCfgMgr.getUserConfigPath();
    if (!exists(userConfigDir))
    {
        std::error_code ec;
        if (!create_directories(userConfigDir, ec))
        {
            cfgError(tr("Error creating OpenMW configuration directory: code %0").arg(ec.value()),
                tr("<br><b>Could not create directory %0</b><br><br>"
                   "%1<br>")
                    .arg(Files::pathToQString(userConfigDir))
                    .arg(QString(ec.message().c_str())));
            return FirstRunDialogResultFailure;
        }
    }

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
    {
        if (!setup())
            return FirstRunDialogResultFailure;
        return FirstRunDialogResultContinue;
    }

    if (mLauncherSettings.isFirstRun())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("First run"));
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(
            tr("<html><head/><body><p><b>Welcome to OpenMW!</b></p>"
               "<p>It is recommended to run the Installation Wizard.</p>"
               "<p>The Wizard will let you select an existing Morrowind installation, "
               "or install Morrowind for OpenMW to use.</p></body></html>"));

        QAbstractButton* wizardButton
            = msgBox.addButton(tr("Run &Installation Wizard"), QMessageBox::AcceptRole); // ActionRole doesn't work?!
        QAbstractButton* skipButton = msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return FirstRunDialogResultWizard;
        }
        else if (msgBox.clickedButton() == skipButton)
        {
            // Don't bother setting up absent game data.
            if (setup())
                return FirstRunDialogResultContinue;
        }
        return FirstRunDialogResultFailure;
    }

    if (!setup() || !setupGameData())
    {
        return FirstRunDialogResultFailure;
    }
    return FirstRunDialogResultContinue;
}

void Launcher::MainDialog::setVersionLabel()
{
    // Add version information to bottom of the window
    QString revision(QString::fromUtf8(Version::getCommitHash().data(), Version::getCommitHash().size()));
    QString tag(QString::fromUtf8(Version::getTagHash().data(), Version::getTagHash().size()));

    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QString productName = ContentSelectorModel::isFallout3Mode(mGameMode) ? tr("OpenFO3") : tr("OpenMW");
    if (!Version::getVersion().empty() && (revision.isEmpty() || revision == tag))
        versionLabel->setText(tr("%1 %2 release")
                                  .arg(productName)
                                  .arg(QString::fromUtf8(Version::getVersion().data(), Version::getVersion().size())));
    else
        versionLabel->setText(tr("%1 development (%2)").arg(productName).arg(revision.left(10)));

    // Add the compile date and time
    auto compileDate = QLocale(QLocale::C).toDate(QString(__DATE__).simplified(), QLatin1String("MMM d yyyy"));
    auto compileTime = QLocale(QLocale::C).toTime(QString(__TIME__).simplified(), QLatin1String("hh:mm:ss"));
    versionLabel->setToolTip(tr("Compiled on %1 %2")
                                 .arg(QLocale::system().toString(compileDate, QLocale::LongFormat),
                                     QLocale::system().toString(compileTime, QLocale::ShortFormat)));
}

bool Launcher::MainDialog::setup()
{
    if (!setupGameSettings())
        return false;

    setVersionLabel();

    mLauncherSettings.setContentList(mGameSettings);
    applyFallout3DefaultContentSelection();

    if (!setupGraphicsSettings())
        return false;

    // Now create the pages as they need the settings
    createPages();

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
    {
        if (!mDataFilesPage->loadSettings())
            return false;
        loadSettings();
        return true;
    }

    // Call this so we can exit on SDL errors before mainwindow is shown
    if (!mGraphicsPage->loadSettings())
        return false;

    loadSettings();

    return true;
}

bool Launcher::MainDialog::reloadSettings()
{
    if (!setupLauncherSettings())
        return false;

    if (!setupGameSettings())
        return false;

    mLauncherSettings.setContentList(mGameSettings);
    applyFallout3DefaultContentSelection();

    if (!setupGraphicsSettings())
        return false;

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
    {
        if (!mDataFilesPage->loadSettings())
            return false;
        loadSettings();
        return true;
    }

    if (!mImportPage->loadSettings())
        return false;

    if (!mDataFilesPage->loadSettings())
        return false;

    if (!mGraphicsPage->loadSettings())
        return false;

    if (!mSettingsPage->loadSettings())
        return false;

    return true;
}

void Launcher::MainDialog::enableDataPage()
{
    pagesWidget->setCurrentIndex(0);
    mImportPage->resetProgressBar();
    dataAction->setChecked(true);
    graphicsAction->setChecked(false);
    importAction->setChecked(false);
    settingsAction->setChecked(false);
}

void Launcher::MainDialog::enableGraphicsPage()
{
    pagesWidget->setCurrentIndex(1);
    mImportPage->resetProgressBar();
    dataAction->setChecked(false);
    graphicsAction->setChecked(true);
    settingsAction->setChecked(false);
    importAction->setChecked(false);
}

void Launcher::MainDialog::enableSettingsPage()
{
    pagesWidget->setCurrentIndex(2);
    mImportPage->resetProgressBar();
    dataAction->setChecked(false);
    graphicsAction->setChecked(false);
    settingsAction->setChecked(true);
    importAction->setChecked(false);
}

void Launcher::MainDialog::enableImportPage()
{
    pagesWidget->setCurrentIndex(3);
    mImportPage->resetProgressBar();
    dataAction->setChecked(false);
    graphicsAction->setChecked(false);
    settingsAction->setChecked(false);
    importAction->setChecked(true);
}

bool Launcher::MainDialog::setupLauncherSettings()
{
    mLauncherSettings.clear();

    mGameMode = loadPersistedGameMode();
    updateModeUi();

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
    {
        const QString path = launcherSettingsPath();
        if (QFile::exists(path))
        {
            Log(Debug::Info) << "Loading FO3 launcher config file:" << path.toUtf8().constData();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                cfgError(tr("Error opening OpenFO3 launcher configuration file"),
                    tr("<br><b>Could not open %0 for reading</b><br><br>"
                       "Please make sure you have the right permissions and try again.<br>")
                        .arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            Misc::ensureUtf8Encoding(stream);
            mLauncherSettings.readFile(stream);
        }

        QSettings settings(modeSettingsPath(), QSettings::IniFormat);
        const int width = settings.value(QLatin1String(windowWidthKey), 0).toInt();
        const int height = settings.value(QLatin1String(windowHeightKey), 0).toInt();
        const int posX = settings.value(QLatin1String(windowPosXKey), 0).toInt();
        const int posY = settings.value(QLatin1String(windowPosYKey), 0).toInt();
        if (width > 0 && height > 0)
        {
            mLauncherSettings.setMainWindow(
                Config::LauncherSettings::MainWindow{ .mWidth = width, .mHeight = height, .mPosX = posX, .mPosY = posY });
        }
        return true;
    }

    const QString path
        = Files::pathToQString(mCfgMgr.getUserConfigPath() / Config::LauncherSettings::sLauncherConfigFileName);

    if (!QFile::exists(path))
        return true;

    Log(Debug::Info) << "Loading config file: " << path.toUtf8().constData();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        cfgError(tr("Error opening OpenMW configuration file"),
            tr("<br><b>Could not open %0 for reading:</b><br><br>%1<br><br>"
               "Please make sure you have the right permissions "
               "and try again.<br>")
                .arg(file.fileName())
                .arg(file.errorString()));
        return false;
    }

    QTextStream stream(&file);
    Misc::ensureUtf8Encoding(stream);

    mLauncherSettings.readFile(stream);

    return true;
}

bool Launcher::MainDialog::setupGameSettings()
{
    mGameSettings.clear();

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
        return loadFo3GameSettings();

    QFile file;

    auto loadFile = [&](const QString& path, bool (Config::GameSettings::*reader)(QTextStream&, const QString&, bool),
                        bool ignoreContent = false) -> std::optional<bool> {
        file.setFileName(path);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                cfgError(tr("Error opening OpenMW configuration file"),
                    tr("<br><b>Could not open %0 for reading</b><br><br>"
                       "Please make sure you have the right permissions "
                       "and try again.<br>")
                        .arg(file.fileName()));
                return {};
            }
            QTextStream stream(&file);
            Misc::ensureUtf8Encoding(stream);

            (mGameSettings.*reader)(stream, QFileInfo(path).dir().path(), ignoreContent);
            file.close();
            return true;
        }
        return false;
    };

    // Load the user config file first, separately
    // So we can write it properly, uncontaminated
    if (!loadFile(Files::getUserConfigPathQString(mCfgMgr), &Config::GameSettings::readUserFile))
        return false;

    for (const auto& path : Files::getActiveConfigPathsQString(mCfgMgr))
    {
        Log(Debug::Info) << "Loading config file: " << path.toUtf8().constData();
        if (!loadFile(path, &Config::GameSettings::readFile))
            return false;
    }

    return true;
}

bool Launcher::MainDialog::setupGameData()
{
    if (ContentSelectorModel::isFallout3Mode(mGameMode))
        return true;

    bool foundData = false;

    // Check if the paths actually contain data files
    for (const auto& path3 : mGameSettings.getDataDirs())
    {
        QDir dir(path3.value);
        QStringList filters;
        filters << "*.esp"
                << "*.esm"
                << "*.omwgame"
                << "*.omwaddon";

        if (!dir.entryList(filters).isEmpty())
        {
            foundData = true;
            break;
        }
    }

    if (!foundData)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error detecting Morrowind installation"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(
            tr("<br><b>Could not find the Data Files location</b><br><br>"
               "The directory containing the data files was not found."));

        QAbstractButton* wizardButton = msgBox.addButton(tr("Run &Installation Wizard..."), QMessageBox::ActionRole);
        QAbstractButton* skipButton = msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        Q_UNUSED(skipButton); // Suppress compiler unused warning

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (!mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return false;
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGraphicsSettings()
{
    Settings::Manager::clear(); // Ensure to clear previous settings in case we had already loaded settings.
    try
    {
        Settings::Manager::load(mCfgMgr);
        return true;
    }
    catch (std::exception& e)
    {
        cfgError(tr("Error reading OpenMW configuration files"),
            tr("<br>The problem may be due to an incomplete installation of OpenMW.<br>"
               "Reinstalling OpenMW may resolve the problem.<br>")
                + e.what());
        return false;
    }
}

void Launcher::MainDialog::loadSettings()
{
    const auto& mainWindow = mLauncherSettings.getMainWindow();
    if (mainWindow.mWidth <= 0 || mainWindow.mHeight <= 0)
        return;
    resize(mainWindow.mWidth, mainWindow.mHeight);
    move(mainWindow.mPosX, mainWindow.mPosY);
}

void Launcher::MainDialog::saveSettings()
{
    mLauncherSettings.setMainWindow(Config::LauncherSettings::MainWindow{
        .mWidth = width(),
        .mHeight = height(),
        .mPosX = pos().x(),
        .mPosY = pos().y(),
    });
    mLauncherSettings.resetFirstRun();
    savePersistedGameMode();
}

bool Launcher::MainDialog::writeSettings()
{
    // Now write all config files
    saveSettings();
    mDataFilesPage->saveSettings();
    const bool fallout3Mode = ContentSelectorModel::isFallout3Mode(mGameMode);

    if (!fallout3Mode)
    {
        mGraphicsPage->saveSettings();
        mImportPage->saveSettings();
        mSettingsPage->saveSettings();
    }

    const auto& userPath = mCfgMgr.getUserConfigPath();

    if (!exists(userPath))
    {
        std::error_code ec;
        if (!create_directories(userPath, ec))
        {
            cfgError(tr("Error creating OpenMW configuration directory: code %0").arg(ec.value()),
                tr("<br><b>Could not create directory %0</b><br><br>"
                   "%1<br>")
                    .arg(Files::pathToQString(userPath))
                    .arg(QString(ec.message().c_str())));
            return false;
        }
    }

    QFile file;
    if (!fallout3Mode)
    {
        // Game settings
        file.setFileName(userPath / Files::openmwCfgFile);

        if (!file.open(QIODevice::ReadWrite | QIODevice::Text))
        {
            // File cannot be opened or created
            cfgError(tr("Error writing OpenMW configuration file"),
                tr("<br><b>Could not open or create %0 for writing</b><br><br>"
                   "Please make sure you have the right permissions "
                   "and try again.<br>")
                    .arg(file.fileName()));
            return false;
        }

        mGameSettings.writeFileWithComments(file);
        file.close();

        // Graphics settings
        const auto settingsPath = mCfgMgr.getUserConfigPath() / "settings.cfg";
        try
        {
            Settings::Manager::saveUser(settingsPath);
        }
        catch (std::exception& e)
        {
            std::string msg = "<br><b>Error writing settings.cfg</b><br><br>"
                + Files::pathToUnicodeString(settingsPath) + "<br><br>" + e.what();
            cfgError(tr("Error writing user settings file"), tr(msg.c_str()));
            return false;
        }
    }

    // Launcher settings
    file.setFileName(launcherSettingsPath());

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate))
    {
        // File cannot be opened or created
        cfgError(ContentSelectorModel::isFallout3Mode(mGameMode)
                     ? tr("Error writing OpenFO3 launcher configuration file")
                     : tr("Error writing Launcher configuration file"),
            tr("<br><b>Could not open or create %0 for writing</b><br><br>"
               "Please make sure you have the right permissions "
               "and try again.<br>")
                .arg(file.fileName()));
        return false;
    }

    QTextStream stream(&file);
    stream.setDevice(&file);
    Misc::ensureUtf8Encoding(stream);

    mLauncherSettings.writeFile(stream);
    file.close();

    return true;
}

void Launcher::MainDialog::closeEvent(QCloseEvent* event)
{
    writeSettings();
    event->accept();
}

void Launcher::MainDialog::wizardStarted()
{
    hide();
}

void Launcher::MainDialog::wizardFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus == QProcess::CrashExit)
        return qApp->quit();

    // HACK: Ensure the pages are created, else segfault
    setup();

    if (setupGameData() && reloadSettings())
        show();
}

void Launcher::MainDialog::play()
{
    if (!writeSettings())
        return qApp->quit();

    if (!ContentSelectorModel::isFallout3Mode(mGameMode) && !mGameSettings.hasMaster())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("No game file selected"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(
            tr("<br><b>You do not have a game file selected.</b><br><br>"
               "OpenMW will not start without a game file selected.<br>"));
        msgBox.exec();
        return;
    }

    // Launch the game detached
    const QString executable = currentExecutableName();
    const QStringList arguments = ContentSelectorModel::isFallout3Mode(mGameMode) ? buildOpenFo3Arguments()
                                                                                   : QStringList();

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
    {
        const QByteArray executableLog = executable.toUtf8();
        const QByteArray argumentsLog = arguments.join(QStringLiteral(" ")).toUtf8();
        Log(Debug::Info) << "Launching" << executableLog.constData() << "with arguments:" << argumentsLog.constData();
    }

    if (ContentSelectorModel::isFallout3Mode(mGameMode))
    {
        QString runtimeHome;
        if (!prepareFallout3RuntimeHome(&runtimeHome))
            return;

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HOME"), runtimeHome);
        Log(Debug::Info) << "Launching OpenFO3 with isolated HOME:" << runtimeHome.toUtf8().constData();
        if (mGameInvoker->startProcess(executable, arguments, environment, true))
            return qApp->quit();
        return;
    }

    if (mGameInvoker->startProcess(executable, arguments, true))
        return qApp->quit();
}

void Launcher::MainDialog::help()
{
    Misc::HelpViewer::openHelp({});
}
