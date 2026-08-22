/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2026 FAAC Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "input.h"
#include <faac.h>
#include "resource.h"
#include "output_path.h"
#include "encode_engine.h"

#define WM_USER_PROGRESS (WM_USER + 101)

static HINSTANCE hInstance;

static char inputFilename[_MAX_PATH];
static char outputFilename[_MAX_PATH];

static BOOL Encoding = FALSE;

enum RateMode {
    RATEMODE_VBR = 0,
    RATEMODE_ABR = 1
};

static progress_info_t g_gui_progress;
static DWORD g_last_progress_post = 0;
static CRITICAL_SECTION g_cs_progress;

static BOOL SelectFileName(HWND hParent, char *filename, BOOL forReading)
{
    OPENFILENAME ofn;

    memset(&ofn, 0, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hParent;
    ofn.hInstance = hInstance;
    ofn.nFilterIndex = 0;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    filename[0] = '\0';
    ofn.lpstrFile = (LPSTR)filename;
    ofn.nMaxFile = _MAX_PATH;

    if (forReading)
    {
        static const char filters[] =
            "Wave Files (*.wav)\0*.wav\0"
            "AIFF Files (*.aif;*.aiff;*.aifc)\0*.aif;*.aiff;*.aifc\0"
            "AU Files (*.au)\0*.au\0"
            "All Files (*.*)\0*.*\0\0";

        ofn.lpstrFilter = filters;
        ofn.lpstrDefExt = "wav";
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrTitle = "Select Source File";

        return GetOpenFileName(&ofn);
    }
    else
    {
        static const char filters[] =
            "MPEG-4 Audio (*.m4a)\0*.m4a\0"
            "AAC Files (*.aac)\0*.aac\0"
            "All Files (*.*)\0*.*\0\0";

        ofn.lpstrFilter = filters;
        ofn.lpstrDefExt = "m4a";
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
        ofn.lpstrTitle = "Select Output File";

        return GetSaveFileName(&ofn);
    }
}

/* Sets the quality/bitrate label + edit box for the given rate mode, shared
   by WM_INITDIALOG's initial state and the IDC_RATEMODE CBN_SELCHANGE
   handler so the two don't drift out of sync. */
static void ApplyRateModeUI(HWND hWnd, int mode)
{
    char text[16];

    if (mode == RATEMODE_VBR)
    {
        SetDlgItemText(hWnd, IDC_QUALITYLABEL, "Quantizer\nquality");
        snprintf(text, sizeof(text), "%d", DEFAULT_QUANT_QUALITY);
    }
    else
    {
        SetDlgItemText(hWnd, IDC_QUALITYLABEL, "Bitrate\n(kbps)");
        snprintf(text, sizeof(text), "%d", DEFAULT_ABR_KBPS);
    }
    SetDlgItemText(hWnd, IDC_QUALITY, text);
}

static void AwakeDialogControls(HWND hWnd)
{
    char szTemp[64];
    pcmfile_t *infile = NULL;

    if ((infile = wav_open_read(inputFilename, 0)) == NULL)
        return;

    unsigned int sampleRate = infile->samplerate;
    unsigned int numChannels = infile->channels;

    wav_close(infile);

    SetDlgItemText(hWnd, IDC_INPUTFILENAME, inputFilename);

    char *derived = get_output_filename(inputFilename, 1 /* GUI always defaults to MP4 */);
    if (derived)
    {
        strncpy(outputFilename, derived, sizeof(outputFilename) - 1);
        outputFilename[sizeof(outputFilename) - 1] = '\0';
        free(derived);
    }

    EnableWindow(GetDlgItem(hWnd, IDC_OUTPUTFILENAME), TRUE);
    EnableWindow(GetDlgItem(hWnd, IDC_SELECT_OUTPUTFILE), TRUE);

    SetDlgItemText(hWnd, IDC_OUTPUTFILENAME, outputFilename);

    wsprintf(szTemp, "%uHz %uch", sampleRate, numChannels);
    SetDlgItemText(hWnd, IDC_INPUTPARAMS, szTemp);

    if (numChannels >= 6)
    {
        EnableWindow(GetDlgItem(hWnd, IDC_USELFE), TRUE);
        CheckDlgButton(hWnd, IDC_USELFE, TRUE);
    }
    else
    {
        EnableWindow(GetDlgItem(hWnd, IDC_USELFE), FALSE);
        CheckDlgButton(hWnd, IDC_USELFE, FALSE);
    }

    EnableWindow(GetDlgItem(hWnd, IDOK), TRUE);
}

static bool GuiProgressCallback(const progress_info_t *info, void *user_data)
{
    HWND hWnd = (HWND)user_data;

    if (!Encoding)
        return false;

    DWORD now = GetTickCount();

    EnterCriticalSection(&g_cs_progress);
    /* Throttle updates to ~20 Hz (50ms interval) or final frame to avoid message queue spamming */
    bool should_post = (info->current_input_samples == info->total_input_samples) ||
                       (g_last_progress_post == 0) ||
                       ((now - g_last_progress_post) >= 50);
    if (should_post)
    {
        g_last_progress_post = now;
        g_gui_progress = *info;
    }
    LeaveCriticalSection(&g_cs_progress);

    if (should_post)
    {
        PostMessage(hWnd, WM_USER_PROGRESS, 0, 0);
    }

    return true;
}

static DWORD WINAPI EncodeFile(LPVOID pParam)
{
    HWND hWnd = (HWND)pParam;

    EnterCriticalSection(&g_cs_progress);
    g_last_progress_post = 0;
    LeaveCriticalSection(&g_cs_progress);

    GetDlgItemText(hWnd, IDC_INPUTFILENAME, inputFilename, sizeof(inputFilename));
    GetDlgItemText(hWnd, IDC_OUTPUTFILENAME, outputFilename, sizeof(outputFilename));

    encode_options_t opts;
    init_encode_options(&opts);

    opts.input_filename = inputFilename;
    opts.output_filename = outputFilename;
    opts.container_mp4 = is_mp4_filename(outputFilename) != 0;
    opts.overwrite = 1;

    {
        HWND hOT = GetDlgItem(hWnd, IDC_OBJECTTYPE);
        LRESULT sel = SendMessage(hOT, CB_GETCURSEL, 0, 0);
        LRESULT data = (sel != CB_ERR) ? SendMessage(hOT, CB_GETITEMDATA, (WPARAM)sel, 0) : CB_ERR;
        opts.object_type = (data != CB_ERR) ? (enum faac_object_type)data : FAAC_OBJ_AUTO;
    }

    {
        LRESULT mode = SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_GETCURSEL, 0, 0);
        opts.joint_mode = (mode == CB_ERR) ? FAAC_JOINT_MIXED : (enum faac_joint_mode)mode;
    }

    opts.use_tns = IsDlgButtonChecked(hWnd, IDC_USETNS) == BST_CHECKED;
    opts.use_lfe = IsDlgButtonChecked(hWnd, IDC_USELFE) == BST_CHECKED ? 1 : 0;
    opts.stream_format = opts.container_mp4 ? FAAC_STREAM_RAW
                         : (IsDlgButtonChecked(hWnd, IDC_USERAW) == BST_CHECKED
                            ? FAAC_STREAM_RAW : FAAC_STREAM_ADTS);

    char szTemp[256];
    GetDlgItemText(hWnd, IDC_QUALITY, szTemp, sizeof(szTemp));

    {
        HWND hRM = GetDlgItem(hWnd, IDC_RATEMODE);
        LRESULT sel = SendMessage(hRM, CB_GETCURSEL, 0, 0);
        LRESULT mode = (sel != CB_ERR) ? SendMessage(hRM, CB_GETITEMDATA, (WPARAM)sel, 0) : RATEMODE_VBR;

        parse_quality_or_bitrate(szTemp, mode == RATEMODE_ABR, &opts);
    }

    if (IsDlgButtonChecked(hWnd, IDC_BWCTL) == BST_CHECKED)
    {
        GetDlgItemText(hWnd, IDC_BANDWIDTH, szTemp, sizeof(szTemp));
        opts.bandwidth = atoi(szTemp);
    }

    SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 1024));
    SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);

    int status = run_encoding_session(&opts, GuiProgressCallback, hWnd);
    if (status == ENCODE_ERROR)
    {
        MessageBox(hWnd, "Encoding failed!", "Error", MB_OK | MB_ICONSTOP);
    }
    else if (status == ENCODE_SUCCESS)
    {
        MessageBeep(MB_OK);
    }

    SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);
    SendMessage(hWnd, WM_SETTEXT, 0, (LPARAM)"FAAC GUI");
    Encoding = FALSE;
    SetDlgItemText(hWnd, IDOK, "Encode");
    return 0;
}

static INT_PTR CALLBACK DialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg)
    {
    case WM_USER_PROGRESS:
        {
            progress_info_t info;
            EnterCriticalSection(&g_cs_progress);
            info = g_gui_progress;
            LeaveCriticalSection(&g_cs_progress);

            if (info.total_input_samples > 0)
            {
                SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETPOS,
                    (WPARAM)(info.current_input_samples * 1024 / info.total_input_samples), 0);

                char HeaderText[64];
                int percent = (int)(info.current_input_samples * 100 / info.total_input_samples);
                snprintf(HeaderText, sizeof(HeaderText), "FAAC GUI: %d%%", percent);
                SetWindowText(hWnd, HeaderText);
            }

            char szTemp[256];
            double playingTime = (double)info.current_input_samples / (double)(info.sample_rate ? info.sample_rate : 1);
            snprintf(szTemp, sizeof(szTemp),
                "Playing time: %02d:%04.1f\tEncoding time: %02d:%04.1f\n"
                "Play/enc factor: %.2f\tEstimated time left: %02d:%04.1f",
                (int)playingTime / 60, (float)((int)(playingTime * 10.0) % 600) / 10.0f,
                (int)(info.time_elapsed_sec / 60.0), (float)((int)(info.time_elapsed_sec * 10.0) % 600) / 10.0f,
                (float)info.speed_factor,
                (int)info.eta_sec / 60, (float)((int)(info.eta_sec * 10.0) % 600) / 10.0f);

            SetDlgItemText(hWnd, IDC_TIME, szTemp);
            return TRUE;
        }

    case WM_INITDIALOG:
        {
            faac_library_info libinfo = { .struct_size = sizeof(libinfo) };
            if (faac_get_library_info(&libinfo) == FAAC_OK)
            {
                char txt[128];
                snprintf(txt, sizeof(txt), "libfaac version %s", libinfo.version ? libinfo.version : "?");
                SetDlgItemText(hWnd, IDC_COMPILEDATE, txt);
            }
            else
            {
                MessageBox(hWnd, "Wrong libfaac version!", "FAAC", MB_OK | MB_ICONERROR);
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            }
        }

        inputFilename[0] = '\0';
        outputFilename[0] = '\0';

        {
            HWND hRM = GetDlgItem(hWnd, IDC_RATEMODE);
            LRESULT idx;
            idx = SendMessage(hRM, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"VBR (Quality)");
            SendMessage(hRM, CB_SETITEMDATA, idx, (LPARAM)RATEMODE_VBR);
            idx = SendMessage(hRM, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"ABR (Bitrate)");
            SendMessage(hRM, CB_SETITEMDATA, idx, (LPARAM)RATEMODE_ABR);
            SendMessage(hRM, CB_SETCURSEL, 0, 0);
        }

        {
            HWND hOT = GetDlgItem(hWnd, IDC_OBJECTTYPE);
            LRESULT idx;
            idx = SendMessage(hOT, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"Auto");
            SendMessage(hOT, CB_SETITEMDATA, idx, (LPARAM)FAAC_OBJ_AUTO);
            idx = SendMessage(hOT, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"Low Complexity");
            SendMessage(hOT, CB_SETITEMDATA, idx, (LPARAM)FAAC_OBJ_LOW);
            idx = SendMessage(hOT, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"HE-AAC v1");
            SendMessage(hOT, CB_SETITEMDATA, idx, (LPARAM)FAAC_OBJ_HE_AAC_V1);
            SendMessage(hOT, CB_SETCURSEL, 0, 0);
        }

        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"None");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"M/S");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"IS");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"Mixed");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_SETCURSEL, 3, 0);

        CheckDlgButton(hWnd, IDC_USELFE, FALSE);
        CheckDlgButton(hWnd, IDC_USERAW, FALSE);
        CheckDlgButton(hWnd, IDC_USETNS, FALSE);
        ApplyRateModeUI(hWnd, RATEMODE_VBR);
        SetDlgItemText(hWnd, IDC_BANDWIDTH, "0");

        DragAcceptFiles(hWnd, TRUE);
        return TRUE;

    case WM_DROPFILES:
        if (DragQueryFile((HDROP)wParam, 0, (LPSTR)inputFilename, _MAX_PATH - 1))
            AwakeDialogControls(hWnd);

        DragFinish((HDROP)wParam);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (!Encoding)
            {
                DWORD retval;
                CreateThread(NULL, 0, EncodeFile, hWnd, 0, &retval);
                Encoding = TRUE;
                SetDlgItemText(hWnd, IDOK, "Stop");
            }
            else
            {
                Encoding = FALSE;
                SetDlgItemText(hWnd, IDOK, "Encode");
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hWnd, TRUE);
            return TRUE;

        case IDC_SELECT_INPUTFILE:
            if (SelectFileName(hWnd, inputFilename, TRUE))
                AwakeDialogControls(hWnd);
            break;

        case IDC_SELECT_OUTPUTFILE:
            if (SelectFileName(hWnd, outputFilename, FALSE))
            {
                SetDlgItemText(hWnd, IDC_OUTPUTFILENAME, outputFilename);
            }
            break;

        case IDC_BWCTL:
            switch (IsDlgButtonChecked(hWnd, IDC_BWCTL))
            {
            case BST_CHECKED:
                EnableWindow(GetDlgItem(hWnd, IDC_BANDWIDTH), TRUE);
                break;
            case BST_UNCHECKED:
                EnableWindow(GetDlgItem(hWnd, IDC_BANDWIDTH), FALSE);
                break;
            }
            break;

        case IDC_RATEMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                HWND hRM = GetDlgItem(hWnd, IDC_RATEMODE);
                LRESULT sel = SendMessage(hRM, CB_GETCURSEL, 0, 0);
                LRESULT mode = (sel != CB_ERR) ? SendMessage(hRM, CB_GETITEMDATA, (WPARAM)sel, 0) : RATEMODE_VBR;

                ApplyRateModeUI(hWnd, (int)mode);
            }
            break;
        }
        break;
    }

    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    hInstance = hInst;
    InitializeCriticalSection(&g_cs_progress);
    int res = (int)DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAINDIALOG), NULL, DialogProc);
    DeleteCriticalSection(&g_cs_progress);
    return res;
}
