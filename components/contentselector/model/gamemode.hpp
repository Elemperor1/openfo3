#ifndef CONTENTSELECTORMODEL_GAMEMODE_HPP
#define CONTENTSELECTORMODEL_GAMEMODE_HPP

#include <QString>

namespace ContentSelectorModel
{
    enum GameMode
    {
        GameMode_OpenMW,
        GameMode_Fallout3
    };

    inline constexpr bool isFallout3Mode(GameMode mode)
    {
        return mode == GameMode_Fallout3;
    }

    inline QString gameModeToString(GameMode mode)
    {
        return isFallout3Mode(mode) ? QStringLiteral("fallout3") : QStringLiteral("openmw");
    }

    inline GameMode gameModeFromString(const QString& value, GameMode fallback = GameMode_OpenMW)
    {
        if (value.compare(QStringLiteral("fallout3"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("fo3"), Qt::CaseInsensitive) == 0)
            return GameMode_Fallout3;
        if (value.compare(QStringLiteral("openmw"), Qt::CaseInsensitive) == 0)
            return GameMode_OpenMW;
        return fallback;
    }
}

#endif // CONTENTSELECTORMODEL_GAMEMODE_HPP
