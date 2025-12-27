#pragma once

#include "common.h"

// Notification constants
#define NOTIF_WIDTH 364
#define NOTIF_HEIGHT 200
#define NOTIF_PREVIEW_WIDTH 332
#define NOTIF_PREVIEW_HEIGHT 100
#define NOTIF_BTN_HEIGHT 32
#define NOTIF_TIMER_ID 1
#define NOTIF_ANIM_TIMER_ID 2
#define NOTIF_DURATION 5000
#define NOTIF_ANIM_DURATION 200
#define NOTIF_ANIM_STEPS 15

struct NotifButton {
    RECT rect;
    const wchar_t* icon;
    const wchar_t* label;
};

extern NotifButton g_notifButtons[3];
extern int g_notifAnimStep;
extern int g_notifTargetX;
extern int g_notifStartX;
extern bool g_notifClosing;

HBITMAP CreatePreviewBitmap(const wchar_t* filepath, int width, int height);
LRESULT CALLBACK NotificationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowNotification(const wchar_t* filepath);
void HideNotification();
