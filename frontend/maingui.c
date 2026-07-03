/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: maingui.c,v 1.21 2007/03/19 19:57:40 menno Exp $
 */

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdlib.h>

#include "input.h"
#include "utils.h"
#include "mp4write.h"

#include <faac.h>
#include "resource.h"


static HINSTANCE hInstance;

static char inputFilename[_MAX_PATH], outputFilename[_MAX_PATH];

static BOOL Encoding = FALSE;

static BOOL SelectFileName(HWND hParent, char *filename, BOOL forReading)
{
    OPENFILENAME ofn;

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hParent;
    ofn.hInstance = hInstance;
    ofn.nFilterIndex = 0;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 31;
    filename [0] = 0x00;
    ofn.lpstrFile = (LPSTR)filename;
    ofn.nMaxFile = _MAX_PATH;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter = 0;
    ofn.nFileOffset = 0;
    ofn.nFileExtension = 0;
    ofn.lCustData = 0;
    ofn.lpfnHook = NULL;
    ofn.lpTemplateName = NULL;

    if (forReading)
    {
        char filters[] = { "Wave Files (*.wav)\0*.wav\0" \
            "AIFF Files (*.aif;*.aiff;*.aifc)\0*.aif;*.aiff;*.aifc\0" \
            "AU Files (*.au)\0*.au\0" \
            "All Files (*.*)\0*.*\0\0" };

        ofn.lpstrFilter = filters;
        ofn.lpstrDefExt = "wav";

        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrTitle = "Select Source File";

        return GetOpenFileName (&ofn);
    } else {
        char filters [] = { "MP4 Files (*.m4a)\0*.m4a\0" \
            "AAC Files (*.aac)\0*.aac\0" \
            "All Files (*.*)\0*.*\0\0" };

        ofn.lpstrFilter = filters;
        ofn.lpstrDefExt = "m4a";

        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
        ofn.lpstrTitle = "Select Output File";

        return GetSaveFileName(&ofn);
    }
}

static void AwakeDialogControls(HWND hWnd)
{
    char szTemp[64];
    pcmfile_t *infile = NULL;
    unsigned int sampleRate, numChannels;
    char *pExt;

    if ((infile = wav_open_read(inputFilename, 0)) == NULL)
        return;

    /* determine input file parameters */
    sampleRate = infile->samplerate;
    numChannels = infile->channels;

    wav_close(infile);

    SetDlgItemText (hWnd, IDC_INPUTFILENAME, inputFilename);

    {
        char *newOutput = faac_get_output_filename(inputFilename, 1);
        if (newOutput)
        {
            strncpy(outputFilename, newOutput, sizeof(outputFilename) - 1);
            outputFilename[sizeof(outputFilename) - 1] = '\0';
            free(newOutput);
        }
    }

    EnableWindow(GetDlgItem(hWnd, IDC_OUTPUTFILENAME), TRUE);
    EnableWindow(GetDlgItem(hWnd, IDC_SELECT_OUTPUTFILE), TRUE);

    SetDlgItemText(hWnd, IDC_OUTPUTFILENAME, outputFilename);

    wsprintf(szTemp, "%iHz %ich", sampleRate, numChannels);
    SetDlgItemText(hWnd, IDC_INPUTPARAMS, szTemp);

    EnableWindow(GetDlgItem(hWnd, IDOK), TRUE);
}

static DWORD WINAPI EncodeFile(LPVOID pParam)
{
    HWND hWnd = (HWND) pParam;
    pcmfile_t *infile = NULL;

    GetDlgItemText(hWnd, IDC_INPUTFILENAME, inputFilename, sizeof(inputFilename));
    GetDlgItemText(hWnd, IDC_OUTPUTFILENAME, outputFilename, sizeof(outputFilename));

    /* open the input file */
    if ((infile = wav_open_read(inputFilename, 0)) != NULL)
    {
        /* determine input file parameters */
        unsigned int sampleRate = infile->samplerate;
        unsigned int numChannels = infile->channels;

        unsigned long inputSamples;
        unsigned long maxOutputBytes;

        /* open and setup the encoder */
        faacEncHandle hEncoder = faacEncOpen(sampleRate, numChannels,
            &inputSamples, &maxOutputBytes);

        if (hEncoder)
        {
            HANDLE hOutfile = INVALID_HANDLE_VALUE;
            int use_mp4 = faac_is_mp4_filename(outputFilename);
	    char szTemp[256];

            /* set encoder configuration */
            faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(hEncoder);

            {
                LRESULT mode = SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_GETCURSEL, 0, 0);
                config->jointmode = (mode == CB_ERR) ? JOINT_MIXED : (unsigned int)mode;
            }
            config->useTns = IsDlgButtonChecked(hWnd, IDC_USETNS) == BST_CHECKED ? 1 : 0;
            config->useLfe = IsDlgButtonChecked(hWnd, IDC_USELFE) == BST_CHECKED ? 1 : 0;
            config->outputFormat = IsDlgButtonChecked(hWnd, IDC_USERAW) == BST_CHECKED ? 0 : 1;

            if (use_mp4)
            {
                config->mpegVersion = MPEG4;
                config->outputFormat = RAW_STREAM;
            }
            else
            {
                config->mpegVersion = SendMessage(GetDlgItem(hWnd, IDC_MPEGVERSION), CB_GETCURSEL, 0, 0);
            }
            config->aacObjectType = SendMessage(GetDlgItem(hWnd, IDC_OBJECTTYPE), CB_GETCURSEL, 0, 0);
            if (config->aacObjectType == CB_ERR) config->aacObjectType = LOW;

            GetDlgItemText(hWnd, IDC_QUALITY, szTemp, sizeof(szTemp));
	    config->quantqual = atoi(szTemp);
	    if (IsDlgButtonChecked(hWnd, IDC_BWCTL) == BST_CHECKED)
	    {
            GetDlgItemText(hWnd, IDC_BANDWIDTH, szTemp, sizeof(szTemp));
            config->bandWidth = atoi(szTemp);
	    }
	    else
	      config->bandWidth = 0;

            if (!faacEncSetConfiguration(hEncoder, config))
            {
                faacEncClose(hEncoder);
                wav_close(infile);

                MessageBox (hWnd, "faacEncSetConfiguration failed!", "Error", MB_OK | MB_ICONSTOP);

                SendMessage(hWnd,WM_SETTEXT,0,(LPARAM)"FAAC GUI");
                Encoding = FALSE;
                SetDlgItemText(hWnd, IDOK, "Encode");

                return 0;
            }

	    sprintf(szTemp, "%ld", config->quantqual);
	    SetDlgItemText(hWnd, IDC_QUALITY, szTemp);

	    sprintf(szTemp, "%d", config->bandWidth);
	    SetDlgItemText(hWnd, IDC_BANDWIDTH, szTemp);

            /* open the output file */
            if (use_mp4)
            {
                if (mp4_open(outputFilename, 1))
                {
                    MessageBox(hWnd, "Couldn't create MP4 output file!", "Error", MB_OK | MB_ICONSTOP);
                    faacEncClose(hEncoder);
                    wav_close(infile);
                    return 0;
                }
                mp4_set_format(sampleRate, numChannels, infile->samplebytes * 8);
            }
            else
            {
                hOutfile = CreateFile(outputFilename, GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            }

            if (use_mp4 || hOutfile != INVALID_HANDLE_VALUE)
            {
                UINT startTime = GetTickCount(), lastUpdated = 50;
                DWORD totalBytesRead = 0;

                int bytesWritten = 0;
                unsigned int bytesInput = 0;
                DWORD numberOfBytesWritten = 0;
                int *pcmbuf;
                unsigned char *bitbuf;
                char HeaderText[50];
                char Percentage[5];

                pcmbuf = (int*)malloc(inputSamples*sizeof(int));
                bitbuf = (unsigned char*)malloc(maxOutputBytes*sizeof(unsigned char));

                SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 1024));
                SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);

                for ( ;; )
                {
                    UINT timeElapsed;

                    bytesInput = wav_read_int24(infile, pcmbuf, inputSamples, NULL) * sizeof(int);

                    SendDlgItemMessage (hWnd, IDC_PROGRESS, PBM_SETPOS, (unsigned long)((float)totalBytesRead * 1024.0f / (infile->samples*sizeof(int)*numChannels)), 0);

                    /* Percentage for Dialog Output */
                    _itoa((int)((float)totalBytesRead * 100.0f / (infile->samples*sizeof(int)*numChannels)),Percentage,10);
                    lstrcpy(HeaderText,"FAAC GUI: ");
                    lstrcat(HeaderText,Percentage);
                    lstrcat(HeaderText,"%");
                    SendMessage(hWnd,WM_SETTEXT,0,(LPARAM)HeaderText);

                    totalBytesRead += bytesInput;

                    timeElapsed = (GetTickCount () - startTime);
                    // Calculate playing time in seconds from samples per channel
                    double playingTime = (double)totalBytesRead / (numChannels * sizeof(int) * sampleRate);

                    if (timeElapsed > (lastUpdated + 200))
                    {
                        double factor;
                        double timeLeft;

                        lastUpdated = timeElapsed;

                        factor = faac_calc_speed(totalBytesRead / (numChannels * sizeof(int)), sampleRate, (double)timeElapsed / 1000.0);
                        timeLeft = faac_calc_eta(totalBytesRead / (numChannels * sizeof(int)), infile->samples, (double)timeElapsed / 1000.0);

                        sprintf(szTemp, "Playing time: %2.2i:%04.1f\tEncoding time: %2.2i:%04.1f\n"
                                "Play/enc factor: %.2f\tEstimated time left: %2.2i:%04.1f",
                                (int)playingTime / 60, (float)((int)(playingTime * 10) % 600) / 10.0f,
                                timeElapsed / 60000, 0.001 * (timeElapsed % 60000),
                                (float)factor,
                                (int)timeLeft / 60, (float)((int)(timeLeft * 10) % 600) / 10.0f
                               );

                        SetDlgItemText(hWnd, IDC_TIME, szTemp);
                    }

                    /* call the actual encoding routine */
                    bytesWritten = faacEncEncode(hEncoder,
                        pcmbuf,
                        bytesInput/sizeof(int),
                        bitbuf,
                        maxOutputBytes);

                    /* Stop Pressed */
                    if ( !Encoding )
                        break;

                    /* all done, bail out */
                    if (!bytesInput && !bytesWritten)
                        break;

                    if (bytesWritten < 0)
                    {
                        MessageBox (hWnd, "faacEncEncodeFrame failed!", "Error", MB_OK | MB_ICONSTOP);
                        break;
                    }

                    if (use_mp4)
                    {
                        mp4_write_frame(bitbuf, (uint32_t)bytesWritten, 1024);
                    }
                    else
                    {
                        WriteFile(hOutfile, bitbuf, bytesWritten, &numberOfBytesWritten, NULL);
                    }

                }

                /* flush encoder */
                do {
                    bytesWritten = faacEncEncode(hEncoder, NULL, 0, bitbuf, maxOutputBytes);
                    if (bytesWritten > 0)
                    {
                        if (use_mp4)
                            mp4_write_frame(bitbuf, (uint32_t)bytesWritten, 1024);
                        else
                            WriteFile(hOutfile, bitbuf, bytesWritten, &numberOfBytesWritten, NULL);
                    }
                } while (bytesWritten > 0);

                if (use_mp4)
                {
                    char *id_string, *copyright_string;
                    faac_check_version(&id_string, &copyright_string);
                    faac_mp4_finish(hEncoder, id_string);
                }
                else
                {
                    CloseHandle(hOutfile);
                }

                if (pcmbuf) free(pcmbuf);
                if (bitbuf) free(bitbuf);
            }

            faacEncClose(hEncoder);
        }

        wav_close(infile);
        MessageBeep(1);

        SendDlgItemMessage(hWnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);
    } else {
        MessageBox(hWnd, "Couldn't open input file!", "Error", MB_OK | MB_ICONSTOP);
    }

    SendMessage(hWnd,WM_SETTEXT,0,(LPARAM)"FAAC GUI");
    Encoding = FALSE;
    SetDlgItemText(hWnd, IDOK, "Encode");
    return 0;
}

static BOOL WINAPI DialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
      {
        char *id_string, *copyright_string;
        if (faac_check_version(&id_string, &copyright_string))
        {
          char txt[100];
          sprintf(txt, "libfaac version %s", id_string);
          SetDlgItemText(hWnd, IDC_COMPILEDATE, txt);
        }
        else
        {
          MessageBox(hWnd, "wrong libfaac version", "FAAC",
                     MB_OK | MB_ICONERROR);
          PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
      }

        inputFilename[0] = 0x00;

        SendMessage(GetDlgItem(hWnd, IDC_MPEGVERSION), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"MPEG4");
        SendMessage(GetDlgItem(hWnd, IDC_MPEGVERSION), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"MPEG2");
        SendMessage(GetDlgItem(hWnd, IDC_MPEGVERSION), CB_SETCURSEL, 0, 0);

        SendMessage(GetDlgItem(hWnd, IDC_OBJECTTYPE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"Low Complexity");
        SendMessage(GetDlgItem(hWnd, IDC_OBJECTTYPE), CB_SETCURSEL, 0, 0);

        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"None");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"M/S");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"IS");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)"Mixed");
        SendMessage(GetDlgItem(hWnd, IDC_JOINTMODE), CB_SETCURSEL, 3, 0);

        CheckDlgButton(hWnd, IDC_USELFE, FALSE);
        CheckDlgButton(hWnd, IDC_USERAW, FALSE);
        CheckDlgButton(hWnd, IDC_USETNS, TRUE);
        SetDlgItemText(hWnd, IDC_QUALITY, "100");
        SetDlgItemText(hWnd, IDC_BANDWIDTH, "0");

        DragAcceptFiles(hWnd, TRUE);
        return TRUE;

    case WM_DROPFILES:

        if (DragQueryFile((HDROP) wParam, 0, (LPSTR) inputFilename, _MAX_PATH - 1))
            AwakeDialogControls(hWnd);

        DragFinish((HDROP) wParam);
        return FALSE;

    case WM_COMMAND:

        switch (wParam)
        {
        case IDOK:

            if ( !Encoding )
            {
                DWORD retval;
                CreateThread(NULL,0,EncodeFile,hWnd,0,&retval);
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
	    //SetDlgItemText(hWnd, IDC_BANDWIDTH, "0");
            break;
	  case BST_UNCHECKED:
	    EnableWindow(GetDlgItem(hWnd, IDC_BANDWIDTH), FALSE);
	    //SetDlgItemText(hWnd, IDC_BANDWIDTH, "");
            break;
	  }
	  break;
        }

        break;
    }

    return FALSE;
}

int WINAPI WinMain (HINSTANCE hInst, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hInstance = hInst;

    return DialogBox(hInstance, MAKEINTRESOURCE (IDD_MAINDIALOG), NULL, (DLGPROC) DialogProc);
}
