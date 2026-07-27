#pragma once

#include <Windows.h>

namespace FancyZonesFullscreen
{
    constexpr ULONGLONG exitIntentGraceMs = 500;

    enum class ExitIntentAction
    {
        Continue,
        SuppressCorrections,
        StopTracking,
    };

    constexpr bool IsExitIntentKey(DWORD virtualKey,
                                   bool shiftDown,
                                   bool winDown,
                                   bool altDown,
                                   bool ctrlDown) noexcept
    {
        return !shiftDown &&
               !winDown &&
               !altDown &&
               !ctrlDown &&
               (virtualKey == VK_ESCAPE || virtualKey == VK_F11);
    }

    constexpr ExitIntentAction EvaluateExitIntent(bool hasFullscreenStyle,
                                                  ULONGLONG exitIntentUntil,
                                                  ULONGLONG now) noexcept
    {
        if (!hasFullscreenStyle)
        {
            return ExitIntentAction::StopTracking;
        }

        if (exitIntentUntil > now)
        {
            return ExitIntentAction::SuppressCorrections;
        }

        return ExitIntentAction::Continue;
    }

    constexpr bool HasExitIntentVerification(ULONGLONG exitIntentUntil) noexcept
    {
        return exitIntentUntil != 0;
    }

    constexpr bool IsExitIntentVerificationDue(ULONGLONG exitIntentUntil, ULONGLONG now) noexcept
    {
        return HasExitIntentVerification(exitIntentUntil) &&
               now >= exitIntentUntil;
    }
}
