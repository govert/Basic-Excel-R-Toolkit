/*
 * Basic Excel R Toolkit (BERT)
 * Copyright (C) 2014 Structured Data, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef __DIALOGS_H
#define __DIALOGS_H

#define DIALOG_RESULT_TYPE BOOL

DIALOG_RESULT_TYPE CALLBACK ConsoleDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);
DIALOG_RESULT_TYPE CALLBACK OptionsDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);
DIALOG_RESULT_TYPE CALLBACK AboutDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);

#define ABOUT_BERT_TEXT \
	"Basic Excel R Toolkit (BERT) v " BERT_VERSION "\r\n" \
	"Copyright (C) 2014 Structured Data, LLC"

#define ABOUT_R_TEXT \
	"Includes R version 3.1.0 (2014-04-10) -- \"Spring Dance\"\r\n" \
	"Copyright (C) 2014 The R Foundation for Statistical Computing"

#define PARSE_ERROR_MESSAGE "(parse error)\n"

#define SCINTILLA_FONT_NAME		"Courier New"
#define SCINTILLA_FONT_SIZE		10

void ConsoleDlg(HINSTANCE hInst);
void AppendLog(const char *buffer); 



#endif // #ifndef __DIALOGS_H


