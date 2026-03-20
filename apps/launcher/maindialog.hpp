#ifndef MAINDIALOG_H
#define MAINDIALOG_H

#ifndef Q_MOC_RUN
#include <components/process/processinvoker.hpp>

#include <components/config/gamesettings.hpp>
#include <components/config/launchersettings.hpp>
#include <components/contentselector/model/gamemode.hpp>

#endif
#include "ui_mainwindow.h"

class QListWidgetItem;
class QComboBox;
class QStackedWidget;
class QStringListModel;
class QString;

namespace Files
{
    struct ConfigurationManager;
}

namespace Launcher
{
    class GraphicsPage;
    class DataFilesPage;
    class UnshieldThread;
    class ImportPage;
    class SettingsPage;

    enum FirstRunDialogResult
    {
        FirstRunDialogResultFailure,
        FirstRunDialogResultContinue,
        FirstRunDialogResultWizard
    };

#ifndef WIN32
    bool expansions(Launcher::UnshieldThread& cd);
#endif

    class MainDialog : public QMainWindow, private Ui::MainWindow
    {
        Q_OBJECT

    public:
        explicit MainDialog(
            const Files::ConfigurationManager& configurationManager, const QString& resourcesPath, QWidget* parent = nullptr);
        ~MainDialog() override;

        FirstRunDialogResult showFirstRunDialog();

        bool reloadSettings();
        bool writeSettings();

    public slots:
        void enableDataPage();
        void enableGraphicsPage();
        void enableSettingsPage();
        void enableImportPage();
        void play();
        void help();

    protected:
        bool event(QEvent* event) override;

    private slots:
        void wizardStarted();
        void wizardFinished(int exitCode, QProcess::ExitStatus exitStatus);

    private:
        bool setup();

        void createIcons();
        void createPages();

        bool setupLauncherSettings();
        bool setupGameSettings();
        bool setupGraphicsSettings();
        bool setupGameData();
        bool loadFo3GameSettings();
        void applyFallout3DefaultContentSelection();
        ContentSelectorModel::GameMode loadPersistedGameMode();
        void savePersistedGameMode() const;
        void updateModeUi();
        QString modeSettingsPath() const;
        QString launcherSettingsPath() const;
        QString currentExecutableName() const;
        QStringList buildOpenFo3Arguments();
        QString fallout3RuntimeHomePath() const;
        bool prepareFallout3RuntimeHome(QString* runtimeHomePath) const;
        QStringList collectFallout3Archives(const QString& dataDir, const QStringList& contentFiles) const;
        QStringList collectFallout3ContentFiles(const QString& dataDir) const;
        QString resolveFallout3DataDir() const;

        void setVersionLabel();

        void loadSettings();
        void saveSettings();

        inline bool startProgram(const QString& name, bool detached = false)
        {
            return startProgram(name, QStringList(), detached);
        }
        bool startProgram(const QString& name, const QStringList& arguments, bool detached = false);

        void closeEvent(QCloseEvent* event) override;
        void gameModeChanged(int index);

        GraphicsPage* mGraphicsPage;
        DataFilesPage* mDataFilesPage;
        ImportPage* mImportPage;
        SettingsPage* mSettingsPage;

        Process::ProcessInvoker* mGameInvoker;
        Process::ProcessInvoker* mWizardInvoker;

        const Files::ConfigurationManager& mCfgMgr;
        QString mResourcesPath;
        QComboBox* mGameModeComboBox;
        ContentSelectorModel::GameMode mGameMode;
        bool mLoadingModeSelection = false;

        Config::GameSettings mGameSettings;
        Config::LauncherSettings mLauncherSettings;
    };
}
#endif
