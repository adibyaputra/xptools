/* 
 * Copyright (c) 2007, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 *
 */

#include "WED_StartWindow.h"
#include "GUI_Button.h"
#include "WED_Menus.h"
#include "GUI_Messages.h"
#include "GUI_DrawUtils.h"
#include "GUI_Resources.h"
#include "BitmapUtils.h"


static int *	SizeOfPng(const char * png)
{
	static int bounds[4];
	ImageInfo	im;
	GUI_GetImageResource(png, &im);
	bounds[0] = 0; bounds[1] = 0;
	bounds[2] = im.width;
	bounds[3] = im.height;
	DestroyBitmap(&im);
	return bounds;	
}

WED_StartWindow::WED_StartWindow(GUI_Commander * cmder) : GUI_Window("WED", xwin_style_movable | xwin_style_centered, SizeOfPng("startup_bkgnd.png"), cmder)
{
	ImageInfo bkgnd, btns;
	GUI_GetImageResource("startup_bkgnd.png", &bkgnd);
	GUI_GetImageResource("startup_btns.png", &btns);	

//	SetBounds(100, 100, bkgnd.width + 100, bkgnd.height + 100);

	int btn_width = btns.width / 2;
	int btn_height = btns.height / 2;
	
	int btn_width1  = btn_width  / 2; int btn_width2  = btn_width  - btn_width1 ;
	int btn_height1 = btn_height / 2; int btn_height2 = btn_height - btn_height1;
	
	int height = bkgnd.height * 0.25;
	int p1 = bkgnd.width * 0.25;
	int p2 = bkgnd.width * 0.75;

	int	new_off[4] = { 0, 0, 2, 2 };
	int	new_on [4] = { 0, 1, 2, 2 };
	int	opn_off[4] = { 1, 0, 2, 2 };
	int	opn_on [4] = { 1, 1, 2, 2 };
	
	mNew  = new GUI_Button("startup_btns.png", btn_Web, new_off, new_on, new_on, new_on);
	mOpen = new GUI_Button("startup_btns.png", btn_Web, opn_off, opn_on, opn_on, opn_on);
	mNew->SetBounds(p1 - btn_width1, height - btn_height1,
					p1 + btn_width2, height + btn_height2);
	mOpen->SetBounds(p2 - btn_width1, height - btn_height1,
					 p2 + btn_width2, height + btn_height2);
	mNew->SetParent(this);
	mOpen->SetParent(this);
	mNew->Show();
	mOpen->Show();
	mNew->AddListener(this);
	mOpen->AddListener(this);
	mNew->SetSticky(1,1,0,0);
	mOpen->SetSticky(1,1,0,0);
	DestroyBitmap(&bkgnd);
	DestroyBitmap(&btns);
}

WED_StartWindow::~WED_StartWindow()
{
}

void	WED_StartWindow::Draw(GUI_GraphState * state)
{
	int me[4], child[4];
	this->GUI_Pane::GetBounds(me);
	mNew->GetBounds(child);
	child[0] = me[0];
	child[2] = me[2];
	
	int kTileAll[4] = { 0, 0, 1, 1 };
	GUI_DrawCentered(state, "startup_bkgnd.png", me, 0, 0, kTileAll, NULL, NULL);
	GUI_DrawStretched(state, "startup_bar.png", child, kTileAll);
}

void	WED_StartWindow::ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							int						inMsg,
							int						inParam)
{
	if (inSrc == mNew && inMsg == GUI_CONTROL_VALUE_CHANGED)	this->DispatchHandleCommand(wed_NewPackage);
	if (inSrc == mOpen && inMsg == GUI_CONTROL_VALUE_CHANGED)	this->DispatchHandleCommand(wed_OpenPackage);
}

int		WED_StartWindow::MouseMove(int x, int y)
{
	mNew->SetHilite(0);
	mOpen->SetHilite(0);
}