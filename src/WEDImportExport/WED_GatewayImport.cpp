/* 
 * Copyright (c) 2014, Laminar Research.
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

#include "XDefs.h"
#include "WED_GatewayImport.h"

#if HAS_GATEWAY

#include "PlatformUtils.h"
#include "GUI_Resources.h"
#include "WED_Url.h"
#include "curl_http.h"
#include <curl/curl.h>
#include <sstream>

#include <json/json.h>


#include "WED_Document.h"
#include "WED_PackageMgr.h"
#include "WED_MapPane.h"

#include "GUI_Application.h"
#include "GUI_Window.h"
#include "GUI_Packer.h"
#include "GUI_Timer.h"

#include "GUI_Label.h"


#include "WED_FilterBar.h"
#include "GUI_Button.h"
#include "WED_Messages.h"
#include "MemFileUtils.h"
#include "FileUtils.h"
//--DSF/AptImport
#include "WED_AptIE.h"
#include "WED_ToolUtils.h"
#include "WED_Airport.h"
#include "WED_DSFImport.h"
#include "WED_Group.h"
//---------------

//--Table Code------------
#include "GUI_Table.h"
#include "GUI_TextTable.h"
#include "WED_Colors.h"

	//--ICAO Table------------
	#include "WED_ICAOTable.h"
	//------------------------//
	//--Version Table---------
	#include "WED_VerTable.h"
	//------------------------//

//------------------------//

//Heuristics for how big to initialize the rawJSONbuf, since it is faster to make a big chunk than make it constantly resize itself
//These should probably change as the gateway gets bigger and the downloads get longer in average charecter length
#define AIRPORTS_GET_SIZE_GUESS 9000000
#define VERSIONS_GET_SIZE_GUESS 4000
#define VERSION_GET_SIZE_GUESS  6000

#if DEV

#if !TEST_AT_START
//If you want to have the zip,dsf.txt, and apt.dat stay on the HDD instead of being deleted instantly
//Cannot be concievably used during TEST_AT_START mode
#define SAVE_ON_HDD 0
#endif
#endif

enum imp_dialog_stages
{
imp_dialog_error,//for file and network errors
imp_dialog_download_ICAO,
imp_dialog_choose_ICAO,//Let the user choose the aiport from the table. The required GET is done before hand
imp_dialog_download_versions,
imp_dialog_choose_versions,//Let user pick scenery pack(s) to download. The required GET is done before hand
imp_dialog_download_specific_version//Download scenery pack, save the contents in the right place, import to WED

};

enum imp_dialog_msg
{
	filter_changed,
	click_next,
	click_back
};

//starting a new one clears off the old one
class RAII_CURL_HNDL
{
public:
	RAII_CURL_HNDL():
		curl_handle(NULL){}

	void create_HNDL( const string& inURL,
					const string& inCert,
					int			  bufferReserveSize)
					
	{
		if(curl_handle != NULL)
		{
			delete curl_handle;
			curl_handle = NULL;
		}

		rawJSONBuf = vector<char>(bufferReserveSize);
		curl_handle = new curl_http_get_file(inURL,&rawJSONBuf,inCert);
	}

	~RAII_CURL_HNDL()
	{
		delete curl_handle;
		curl_handle = NULL;
	}
	curl_http_get_file * const get_curl_handle() { return curl_handle; }
	const vector<char> & get_JSON_BUF() { return rawJSONBuf; }
	
	
private:
	RAII_CURL_HNDL(const RAII_CURL_HNDL & copy);
	RAII_CURL_HNDL & operator= (const RAII_CURL_HNDL & rhs);

	curl_http_get_file * curl_handle;
	
	//a buffer of chars to be filled, reserve a huge amount of space for it cause we'll need it
	vector<char> rawJSONBuf;
};

class RAII_file 
{
public:

 RAII_file(const char * fname, const char * mode) :
  mFile(fopen(fname,mode))
 {
 }

 ~RAII_file()
 {
	if(mFile)
	{
		fclose(mFile);
	}
 }

 int close() 
 { 
	 if(mFile) 
	 { 
		 int retVal = fclose(mFile); 
		 mFile = NULL;
		 return retVal;
	 }
	 return 0;//TODO - if mFile doesn't exist, calling close is harmless so it should return 0 OR calling close is unexpected and client should know?
 }

 operator bool() const { return mFile != NULL; }
 FILE * operator()() { return mFile; }

private:
 FILE * mFile;
};

//--Mem File Utils code for virtually handling the downloaded zip file--
//Returns an empty string if everything went well, the error message if not
string MemFileHandling(const string & zipPath, const string & filePath, const string & ICAO, bool & has_dsf)
//Scope to ensure all the MemFile stuff stays inside here
{
	//A representation of the zipfile
	MFFileSet * zipRep = FileSet_Open(zipPath.c_str());
	if(zipRep == NULL)
	{
#if DEV
		return string("Could not create memory mapped version of " + zipPath + ". Check if the file exists or if you have sufficient permissions");
#else
		return string("Could not open " + zipPath + ". Check if the file exists or if you have sufficient permissions");
#endif
	}
	int fileCount = FileSet_Count(zipRep) - 1;
	/* For all the files
	*  Get the current file inside the memory mapped zip
	*  Get the filename,
	*		if it is "ICAO.txt", mark has_dsf to true
	*  build the file path
	*  Write the file to the harddrive
	*/
	while(fileCount >= 0)
	{
		MFMemFile * currentFile = FileSet_OpenNth(zipRep,fileCount);
		const char * fileName = FileSet_GetNth(zipRep,fileCount);
					
#if !SAVE_ON_HDD//Because, remember we DON'T want this file normally
		if(fileName == (ICAO + "_Scenery_Pack" + ".zip"))
		{
			MemFile_Close(currentFile);
			fileCount--;
			continue;
		}
#endif
		string writeMode;
		if(fileName == (ICAO + ".txt"))
		{
			has_dsf = true;
			writeMode = "w";
		}
		else
		{
			writeMode = "wb";
		}
						
		RAII_file f(string(filePath + fileName).c_str(),writeMode.c_str());

		if(f)
		{
			size_t write_result = fwrite(MemFile_GetBegin(currentFile),sizeof(char),MemFile_GetEnd(currentFile)-MemFile_GetBegin(currentFile),f());
			f.close();

			if(write_result != MemFile_GetEnd(currentFile)-MemFile_GetBegin(currentFile))
			{
				MemFile_Close(currentFile);
				return string("Could not create file at " + string(filePath + fileName) + ", please ensure you have enough hard drive space and permissions");
			}
			MemFile_Close(currentFile);
		}
		else
		{
			MemFile_Close(currentFile);
			return string("Could not create file at " + filePath + fileName + ", please ensure you have sufficient hard drive space and permissions");
		}
		fileCount--;
	}
	FileSet_Close(zipRep);
	return string();
}//end MemFile stuff
//-------------------------------------------------------------

//returns an error string if there is one
string HandleNetworkError(curl_http_get_file * mCurl)
{
	int err = mCurl->get_error();
	bool bad_net = mCurl->is_net_fail();

	stringstream ss;
	ss.str() = "";
	if(err <= CURL_LAST)
	{
		string msg = curl_easy_strerror((CURLcode) err);
		ss << "Download failed: " << msg << ". (" << err << ")";
				
		if(bad_net) ss << "(Please check your internet connectivity.)";
	}
	else if(err >= 100)
	{
		//Get the string of error data
		vector<char>	errdat;
		mCurl->get_error_data(errdat);
				
		bool is_ok = !errdat.empty();
		for(vector<char>::iterator i = errdat.begin(); i != errdat.end(); ++i)
		{
			//If the charecter is not printable
			if(!isprint(*i))
			{
				is_ok = false;
				break;
			}
		}

		if(is_ok)
		{
			string errmsg = string(errdat.begin(),errdat.end());
			ss << "Error Code " << err << ": " << errmsg;
		}
		else
		{
			//Couldn't get a useful error message, displaying this instead
			ss << "Download failed due to unknown error: " << err << ".";
		}
	}
	else
	{
		ss << "Download failed due to unknown error: " << err << ".";
	}

	return ss.str();	
}

typedef vector<char> JSON_BUF;

//Our private class for the import dialog
class WED_GatewayImportDialog : public GUI_Window, public GUI_Listener, public GUI_Timer, public GUI_Destroyable
{
public:
	WED_GatewayImportDialog(WED_Document * resolver, WED_MapPane * pane, GUI_Commander * cmdr);
	~WED_GatewayImportDialog();

private:
	//Fired when selecting the next button
	void Next();

	//Fire when selecting the back button
	void Back();
	
	//For checking on the curl_http download and displaying a progress bar
	virtual	void TimerFired(void);
	virtual void ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							intptr_t    			inMsg,
							intptr_t				inParam);

#if TEST_AT_START
	//So we can start testing stuff as soon as the program opens
	//Allows for testing of the network and the GUI
	friend class WED_AppMain;
#endif

	int					mPhase;//Our simple stage counter for our simple fsm

	WED_Document *		mResolver;
	WED_MapPane *		mMapPane;
	//Our curl handle we'll be using to get the json files, note the s
	RAII_CURL_HNDL		mCurl;

	//The buffers of the specific packs downloaded at the end
	vector<JSON_BUF>	mSpecificBufs;
	
	string				mICAOid;
//--GUI parts

	//Changes the decoration of the GUI window's title, buttons, etc, based on the stage
	void DecorateGUIWindow(string labelDesc="");
	//--For the whole dialog box
	WED_FilterBar	 *		mFilter;
	GUI_Packer *			mPacker;

	GUI_Pane *				mButtonHolder;
	GUI_Button *			mNextButton;
	GUI_Button *			mBackButton;
	GUI_Label *				mLabel;

	static int				import_bounds_default[4];

	//--ICAO Table
	GUI_Packer *			mICAO_Packer;
	GUI_ScrollerPane *		mICAO_Scroller;
	GUI_Table *				mICAO_Table;
	GUI_Header *			mICAO_Header;

	GUI_TextTable			mICAO_TextTable;
	GUI_TextTableHeader		mICAO_TextTableHeader;
	
	//Starts the download of the json file containing the airports list
	void StartICAODownload();
	//From the downloaded JSON, fill the ICAO table
	void FillICAOFromJSON();
		//--ICAO Table Provider/Geometry
		WED_ICAOTable			mICAO_AptProvider;
		AptVector				mICAO_Apts;

		//Sets up the ICAO table GUI and event handlers
		void MakeICAOTable(int bounds[4]);
		//----------------------//
	//----------------------//

	//--Versions Table----------
	GUI_Packer *			mVersions_Packer;
	GUI_ScrollerPane *		mVersions_Scroller;
	GUI_Table *				mVersions_Table;
	GUI_Header *			mVersions_Header;
	//index of the current selection
	set<int>				mVersions_VersionsSelected;
	GUI_TextTable			mVersions_TextTable;
	GUI_TextTableHeader		mVersions_TextTableHeader;
	
	//Starts the download of the json file to populate the versions table
	bool StartVersionsDownload();

	//From the downloaded JSON, fill the versions table
	void FillVersionsFromJSON();
	
	//Attempts to download all the specific versions downloaded
	void StartSpecificVersionDownload(int id);

	//Once a specific version is downloaded this method decodes and imports it into the document
	//Returns a pointer to the last imported airport
	WED_Airport * ImportSpecificVersion(JSON_BUF version_json_buf);

	//Keeps the versions downloading until they have all been (atleast attempted to download)
	//returns false if there is nothing left in the queue
	bool NextVersionsDownload();
		//--Versions Table Provider/Geometry
		WED_VerTable			mVersions_VerProvider;
		VerVector				mVersions_Vers;

		//Sets up the Versions table GUI and event handlers
		void MakeVersionsTable(int bounds[4]);
		//---------------------//
	//--------------------------//
		
//----------------------//
	
};
int WED_GatewayImportDialog::import_bounds_default[4] = { 0, 0, 750, 500 };

//--Implemation of WED_GateWayImportDialog class---------------
WED_GatewayImportDialog::WED_GatewayImportDialog(WED_Document * resolver, WED_MapPane * pane, GUI_Commander * cmdr) :
	GUI_Window("Import from Gateway",xwin_style_visible|xwin_style_centered|xwin_style_resizable|xwin_style_modal,import_bounds_default,cmdr),
	mResolver(resolver),
	mMapPane(pane),
	mPhase(imp_dialog_download_ICAO),
	mICAO_AptProvider(&mICAO_Apts),
	mICAO_TextTable(this,100,0),
	mVersions_VerProvider(&mVersions_Vers),
	mVersions_TextTable(this,100,0)
{
#if !TEST_AT_START
	mResolver->AddListener(this);
#endif
	//mPacker
	int bounds[4];
	mPacker = new GUI_Packer;
	mPacker->SetParent(this);
	mPacker->SetSticky(1,1,1,1);
	mPacker->Show();
	GUI_Pane::GetBounds(bounds);
	mPacker->SetBounds(bounds);
	mPacker->SetBkgkndImage ("gradient.png");

	//Filter
	mFilter = new WED_FilterBar(this,filter_changed,0,"Filter:","",NULL,false);
	mFilter->Show();
	mFilter->SetSticky(1,0,1,1);
	mFilter->SetParent(mPacker);
	mFilter->AddListener(this);
	mPacker->PackPane(mFilter,gui_Pack_Top);

	//--Button Setup
		int k_reg[4] = { 0, 0, 1, 3 };
		int k_hil[4] = { 0, 1, 1, 3 };
	
		mNextButton = new GUI_Button("push_buttons.png",btn_Push,k_reg, k_hil,k_reg,k_hil);
		mNextButton->SetBounds(105,5,205,GUI_GetImageResourceHeight("push_buttons.png") / 3);
		mNextButton->Show();
		mNextButton->SetSticky(0,1,1,0);
		mNextButton->SetDescriptor("Next");
		mNextButton->SetMsg(click_next,0);

		mBackButton = new GUI_Button("push_buttons.png",btn_Push,k_reg, k_hil,k_reg,k_hil);
		mBackButton->SetBounds(5,5,105,GUI_GetImageResourceHeight("push_buttons.png") / 3);
		mBackButton->Show();
		mBackButton->SetSticky(1,1,0,0);
		mBackButton->SetDescriptor("Cancel");
		mBackButton->SetMsg(click_back,0);
	
		mButtonHolder = new GUI_Pane;
		mButtonHolder->SetBounds(0,0,210,GUI_GetImageResourceHeight("push_buttons.png") / 3 + 10);

		mNextButton->SetParent(mButtonHolder);
		mNextButton->AddListener(this);
		mBackButton->SetParent(mButtonHolder);
		mBackButton->AddListener(this);
	
		mButtonHolder->SetParent(mPacker);
		mButtonHolder->Show();
		mButtonHolder->SetSticky(1,1,1,0);
		mPacker->PackPane(mButtonHolder,gui_Pack_Bottom);
	//--------------------------------//

		GUI_Pane * tableHolder = new GUI_Pane();
		tableHolder->SetBounds(0,0,0,0);//Because we are packing in the center these will be completely overriden
		tableHolder->SetParent(mPacker);
		tableHolder->Show();
		tableHolder->SetSticky(1,1,1,1);
		mPacker->PackPane(tableHolder,gui_Pack_Center);
		
		mICAO_Packer = new GUI_Packer;
		mICAO_Packer->SetParent(tableHolder);
		mICAO_Packer->SetSticky(1,1,1,1);
		mICAO_Packer->Show();
		tableHolder->GetBounds(bounds);
		mICAO_Packer->SetBounds(bounds);
		mICAO_Packer->SetBkgkndImage ("gradient.png");

		mVersions_Packer = new GUI_Packer;
		mVersions_Packer->SetParent(tableHolder);
		mVersions_Packer->SetSticky(1,1,1,1);
		mVersions_Packer->Hide();
		tableHolder->GetBounds(bounds);
		mVersions_Packer->SetBounds(bounds);
		mVersions_Packer->SetBkgkndImage ("gradient.png");

		MakeICAOTable(bounds);
		MakeVersionsTable(bounds);

		mLabel = new GUI_Label();
		mLabel->SetParent(this);
		mLabel->SetDescriptor("Download in Progress, Please Wait");
		mLabel->SetImplicitMultiline(true);
		int labelBounds[4] = {70,230,480,270};
		mLabel->SetBounds(labelBounds);
		
		
		mLabel->SetColors(WED_Color_RGBA(wed_Table_Text));
		
		mLabel->SetSticky(1,0,1,1);
		DecorateGUIWindow();
	StartICAODownload();
}

WED_GatewayImportDialog::~WED_GatewayImportDialog()
{

}

void WED_GatewayImportDialog::Next()
{
	
	switch(mPhase)
	{
	case imp_dialog_error:
		this->AsyncDestroy();
		break;
	//case imp_dialog_download_ICAO:
		//break; no next button here
	case imp_dialog_choose_ICAO:
		//Going to show versions
		if(StartVersionsDownload())
		{
			mPhase = imp_dialog_download_versions;
			mICAO_AptProvider.SelectionStart(1);
		}
		else {
			DoUserAlert("Pick an airport to import.");
		}
		break;
	//case imp_dialog_download_versions:
		//break; no next button here
	case imp_dialog_choose_versions:
		mVersions_VerProvider.GetSelection(mVersions_VersionsSelected);

		//Were we able to in the first place?
		bool able_to_start = NextVersionsDownload();
		if(able_to_start == false)
		{
			//This one stays as a user alert because we don't want to leave this window yet.
			DoUserAlert("You must select at least one item in the list");
			return;//
		}
		mPhase = imp_dialog_download_specific_version;
		break;
	//case imp_dialog_download_specific_version:
		//break; no button here
	}
	DecorateGUIWindow();
}

void WED_GatewayImportDialog::Back()
{
	switch(mPhase)
	{
	case imp_dialog_error:
		break;
	case imp_dialog_download_ICAO:
	case imp_dialog_choose_ICAO:
		this->AsyncDestroy();
		break;
	case imp_dialog_download_versions:
		Stop();
		/* intentional, really! */
	case imp_dialog_choose_versions:
		mFilter->ClearFilter();				// Filter from choose version not appropriate for airports?
		mPhase = imp_dialog_choose_ICAO;
//		mVersions_VerProvider.SelectionStart(1);
		break;
	case imp_dialog_download_specific_version:
		break;
	}
	DecorateGUIWindow();//Decorate once we're in the correct place
}

extern "C" void decode( const char * startP, const char * endP, char * destP, char ** out);
void WED_GatewayImportDialog::TimerFired()
{
	if(	mPhase == imp_dialog_download_ICAO ||
		mPhase == imp_dialog_download_versions ||
		mPhase >= imp_dialog_download_specific_version)
	{
		//To avoid user confusion with a potential progress of -1, we'll just call it 0
		int progress = mCurl.get_curl_handle()->get_progress();
		if(progress < 0)
		{
			progress = 0;
		}
		stringstream ss;
		ss << "Download in Progress: " << progress << "% Done";
		DecorateGUIWindow(ss.str());
	}

	if(mCurl.get_curl_handle()->is_done())
	{
		Stop();
		mPhase++;
		DecorateGUIWindow();
		if(mCurl.get_curl_handle()->is_ok())
		{
			if(mPhase == imp_dialog_choose_ICAO)//We just finished downloading the ICAO list
			{
				FillICAOFromJSON();
			}		
			else if(mPhase == imp_dialog_choose_versions)//
			{
				FillVersionsFromJSON();
			}
			else if(mPhase - 1 >= imp_dialog_download_specific_version) // -1 to counter act the mPhase++, >= for the fact we have multiple downloads
			{
				
				//Push back the latest buffer
				mSpecificBufs.push_back(mCurl.get_JSON_BUF());
				
				//Try to start the next download
				bool has_versions_left = NextVersionsDownload();
				
				//We're all done with everything!
				if(has_versions_left == false)
				{
					WED_Thing * wrl = WED_GetWorld(mResolver);
					wrl->StartOperation("Import Scenery Pack");
					
					WED_Airport * last_imported = NULL;
					
					//If it fails anywhere inside it will soon be destroyed
					for (int i = 0; i < mSpecificBufs.size(); i++)
					{
						last_imported = ImportSpecificVersion(mSpecificBufs[i]);
						
						//We completely abort if _anything_ goes wrong
						if(last_imported == NULL)
						{
							wrl->AbortOperation();
							return;
						}
					}
					
					//Set the current airport in the sense of "WED's current airport"
					WED_SetCurrentAirport(mResolver,last_imported);

					//Select the current airport in the sense of selecting something on the map pane
					ISelection * sel = WED_GetSelect(mResolver);
					sel->Clear();
					sel->Insert(last_imported);

					//Zoom to the airport
					mMapPane->ZoomShowSel();

					wrl->CommitOperation();
					this->AsyncDestroy();//All done!
					return;
				}
			}//end if(mPhase == imp_dialog_download_specific_version
		}//end if(mCurl.get_curl_handle()->is_ok())
		else
		{
			string res = HandleNetworkError(mCurl.get_curl_handle());
			if(res != "")
			{
				mPhase = imp_dialog_error;
				DecorateGUIWindow(res);
			}
		}
	}//end if(mCurl.get_curl_handle()->is_done())
}//end WED_GatewayImportDialog::TimerFired()

void WED_GatewayImportDialog::FillICAOFromJSON()
{
	//create a string from the vector of chars
	string rawJSONString = string(mCurl.get_JSON_BUF().begin(),mCurl.get_JSON_BUF().end());

	Json::Value root;
	Json::Reader reader;
	bool success = reader.parse(rawJSONString,root);
	
	//Check for errors
	if(success == false)
	{
		mPhase = imp_dialog_error;

	#if DEV
		DecorateGUIWindow("Airports list is invalid data");
	#else
		DecorateGUIWindow("The download was corrupted, please try again");
	#endif
		return;
	}

	Json::Value mAirportsGET = Json::Value(Json::arrayValue);
	mAirportsGET.swap(root["airports"]);

	//loop through the whole array
	for (int i = 0; i < mAirportsGET.size(); i++)
	{
		//Get the current scenery object
		Json::Value tmp(Json::objectValue);
		tmp = mAirportsGET.operator[](i);//Yes, you need the verbose operator[] form. Yes it's dumb

		if(tmp["AcceptedSceneryCount"].asInt() > 0)
		{
			AptInfo_t cur_airport;
			cur_airport.icao = tmp["AirportCode"].asString();
			cur_airport.name = tmp["AirportName"].asString();

			//Add the current scenery object's airport code
			mICAO_Apts.push_back(cur_airport);
		}
	}
	mICAO_AptProvider.AptVectorChanged();
}

void WED_GatewayImportDialog::FillVersionsFromJSON()
{
	//create a string from the vector of chars
	string rawJSONString = string(mCurl.get_JSON_BUF().begin(),mCurl.get_JSON_BUF().end());

	//Now that we have our rawJSONString we'll be turning it into a JSON object
	Json::Value root(Json::objectValue);
	
	Json::Reader reader;
	bool success = reader.parse(rawJSONString,root);
	
	//Check for errors
	if(success == false)
	{
		mPhase = imp_dialog_error;
		
	#if DEV
		DecorateGUIWindow("Scenery list is invalid data");
	#else
		DecorateGUIWindow("The download was corrupted, please try again");
	#endif
		return;
	}
	Json::Value airport = root["airport"];
	
	
	Json::Value sceneryArray = Json::Value(Json::arrayValue);
	sceneryArray = airport["scenery"];
	
	//Clear the previous list no matter what
	mVersions_Vers.clear();
	//Build up the table
	for (Json::ValueIterator itr = sceneryArray.begin(); itr != sceneryArray.end(); itr++)
	{
		Json::Value curScenery = *itr;
		VerInfo_t tmp;
					
		tmp.sceneryId = curScenery.operator[]("sceneryId").asInt();
		tmp.isRecommended = tmp.sceneryId == airport.operator[]("recommendedSceneryId").asInt();
		
		tmp.parentId = curScenery.operator[]("parentId").asInt();
		tmp.userId = curScenery.operator[]("userId").asInt();
		tmp.userName = curScenery.operator[]("userName").asString() != "" ? curScenery.operator[]("userName").asString() : "N/A";
		//Dates will appear as ISO8601: https://en.wikipedia.org/wiki/ISO_8601
		//For example 2014-07-31T14:34:47.000Z
																			//If the date string exists go with the date string, else go with a default
		tmp.dateUploaded = curScenery.operator[]("dateUpload").asString() != "" ? curScenery.operator[]("dateUpload").asString() : "0000-00-00T00:00:00.000Z";
		tmp.dateAccepted = curScenery.operator[]("dateAccepted").asString() != "" ? curScenery.operator[]("dateAccepted").asString() : "0000-00-00T00:00:00.000Z";
		tmp.dateApproved = curScenery.operator[]("dateApproved").asString() != "" ? curScenery.operator[]("dateApproved").asString() : "0000-00-00T00:00:00.000Z";
		tmp.type = curScenery.operator[]("type").asString();		//2 for 2D =  3 for 3D
		tmp.status = curScenery["Status"].asString();
		
		//TODO when the "features" part is nailed down what it is -tmp.features = curScenery.operator[]("features").asString();
		tmp.artistComments = curScenery.operator[]("artistComments").asString() != "" ? curScenery.operator[]("artistComments").asString() : "N/A";
		tmp.moderatorComments = curScenery.operator[]("moderatorComments").asString() != "" ? curScenery.operator[]("moderatorComments").asString() : "N/A";
		
		//Catches cases where acceptedSceneryCount > 0 && some pack only has uploadeded status
		if(!(tmp.dateAccepted == "0000-00-00T00:00:00.000Z" && tmp.dateApproved == "0000-00-00T00:00:00.000Z"))
		{
			mVersions_Vers.push_back(tmp);
		}
	}
	mVersions_VerProvider.VerVectorChanged();
}

void WED_GatewayImportDialog::ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							intptr_t    			inMsg,
							intptr_t				inParam)
{
	switch(inMsg) 
	{
	case filter_changed:	
		mICAO_AptProvider.SetFilter(mFilter->GetText());
		mVersions_VerProvider.SetFilter(mFilter->GetText());
		break;
	case click_next:
		Next();
		break;
	case msg_DocumentDestroyed:
		this->AsyncDestroy();
		break;
	case click_back:
		Back();
		break;
	}
}

void WED_GatewayImportDialog::StartICAODownload()
{
	string url = WED_URL_GATEWAY_API;
	
	//Get Certification
	string cert;
	if(!GUI_GetTempResourcePath("gateway.crt", cert))
	{
		mPhase = imp_dialog_error;
		DecorateGUIWindow("This copy of WED is damaged - the certificate for the X-Plane airport gateway is missing.");
		return;
	}

	//Makes the url "https://gatewayapi.x-plane.com:3001/apiv1/airports"
	url += "airports";

	//Get it from the server
	mCurl.create_HNDL(url,cert,AIRPORTS_GET_SIZE_GUESS);
	Start(0.1);
	mLabel->Show();
}

//Starts the download process
bool WED_GatewayImportDialog::StartVersionsDownload()
{
	//Two steps,
	//1.Get the airport from the current selection, then get its sceneryid from mICAO_Apts

	//index of the current selection
	set<int> out_selection;
	mICAO_AptProvider.GetSelection(out_selection);
	if(out_selection.empty())
		return false;
	
	//Current airport selected
	AptInfo_t current_apt = mICAO_Apts.at(*out_selection.begin());

	mICAOid = current_apt.icao;
	
	//Get Certification
	string cert;
	if(!GUI_GetTempResourcePath("gateway.crt", cert))
	{
		mPhase = imp_dialog_error;
		DecorateGUIWindow("This copy of WED is damaged - the certificate for the X-Plane airport gateway is missing.");
		return false;
	}
	
	string url = WED_URL_GATEWAY_API;
	//Makes the url "https://gatewayapi.x-plane.com:3001/apiv1/airport/ICAO"
	url += "airport/" + mICAOid;

	//Get it from the server
	mCurl.create_HNDL(url,cert,VERSIONS_GET_SIZE_GUESS);
	Start(0.1);
	mLabel->Show();
	return true;
}

void WED_GatewayImportDialog::StartSpecificVersionDownload(int id)
{
	//Get Certification
	string cert;
	if(!GUI_GetTempResourcePath("gateway.crt", cert))
	{
		mPhase = imp_dialog_error;
		DecorateGUIWindow("This copy of WED is damaged - the certificate for the X-Plane airport gateway is missing.");
		return;
	}
	
	stringstream url; 
	
	url << WED_URL_GATEWAY_API << "scenery/" << id;
	mCurl.create_HNDL(url.str(),cert,VERSION_GET_SIZE_GUESS);
	Start(0.1);
	mLabel->Show();
}

bool WED_GatewayImportDialog::NextVersionsDownload()
{
	if(mVersions_VersionsSelected.size() == 0)
	{
		Stop();
		return false;
	}
	std::set<int>::iterator index = mVersions_VersionsSelected.begin();
	
	int id = mVersions_Vers[*index].sceneryId;
	//Start the download
	StartSpecificVersionDownload(id);

	//Erase that one off the queue
	mVersions_VersionsSelected.erase(mVersions_VersionsSelected.begin());
	return true;
}

WED_Airport * WED_GatewayImportDialog::ImportSpecificVersion(JSON_BUF version_json_buf)
{
	//create a string from the vector of chars
	string rawJSONString = string(version_json_buf.begin(),version_json_buf.end());

	//Now that we have our rawJSONString we'll be turning it into a JSON object
	Json::Value root = Json::Value(Json::objectValue);
	Json::Reader reader;
	bool success = reader.parse(rawJSONString,root);

	//Check for errors
	if(success == false)
	{
		mPhase = imp_dialog_error;
#if DEV
		DecorateGUIWindow("Downloaded JSON is invalid");
#else
		DecorateGUIWindow("The download was corrupted, please try again");//User facing
#endif
		this->AsyncDestroy();
		return NULL;
	}

	string zipString = root["scenery"]["masterZipBlob"].asString();
	vector<char> outString = vector<char>(zipString.length());

	char * outP;
	decode(&*zipString.begin(),&*zipString.end(),&*outString.begin(),&outP);
				
	//Fixes the terrible vector padding bug by shrinking it back down to precisely the correct size
	outString.resize(outP - &*outString.begin());

#if TEST_AT_START
	//Anything beyond cannot be tested at the start or wouldn't be useful.
	return NULL;
#endif
	string filePath("");

	ILibrarian * lib = WED_GetLibrarian(mResolver);
    lib->LookupPath(filePath);

	string zipPath = filePath + mICAOid + ".zip";

	if(!outString.empty())
	{
		RAII_file f(zipPath.c_str(),"wb");
		if(f)
		{
			size_t write_result = fwrite(&outString[0], sizeof(char), outString.size(), f());
			
			if(write_result != outString.size())
			{
				mPhase = imp_dialog_error;
#if DEV
				stringstream ss;
				ss << write_result;
				
				string wr = ss.str();
				ss.str() = "";
				ss << outString.size();
				string out_s = ss.str();
				DecorateGUIWindow("Could not create file at " + zipPath + ", write_result: " + wr + ", outstring.size(): " + out_s);
#else
				DecorateGUIWindow("Could not create file at " + zipPath + ", please ensure you have enough space and sufficient permissions");
#endif
				int removeVal = FILE_delete_file(zipPath.c_str(),0);
				if(removeVal != 0)
				{
					//DoUserAlert(string("Could not remove temporary file " + zipPath + ". You may delete this file if you wish").c_str());//TODO - is this not helpful to the user?
				}
				return NULL;//Exit before we can continue
			}
		}
		else
		{
			mPhase = imp_dialog_error;
			DecorateGUIWindow("Could not create file at " + zipPath + ", please ensure you have enough space and sufficient permissions");
			return NULL;
		}
	}

#if DEV
	//This line makes it easier to edit the zip path in the memory editor
	//DO NOT USE edit_me
	const char * edit_me = zipPath.c_str();
#endif

	bool has_dsf = false;

	string res = MemFileHandling(zipPath,filePath,mICAOid,has_dsf);

	if(res.size() != 0)
	{
		mPhase = imp_dialog_error;
		DecorateGUIWindow(res);
		return NULL;
	}

	WED_Thing * wrl = WED_GetWorld(mResolver);
	
	string aptdatPath = filePath + mICAOid + ".dat";
	
	//The one out_apt will be the WED_Thing we'll be putting the rest of our
	//Operation inside of
	vector<WED_Airport *> out_apt;
	WED_ImportOneAptFile(aptdatPath,wrl,&out_apt);

	WED_Airport * g = out_apt[0];
	g->SetSceneryID(root["scenery"]["sceneryId"].asInt());

	string dsfTextPath = filePath + mICAOid + ".txt";
	if(has_dsf)
	{
		WED_DoImportText(dsfTextPath.c_str(), (WED_Thing *) g);
	}
	
#if !SAVE_ON_HDD
	//clean up our files ICAOid.dat and potentially ICAOid.txt
	if(has_dsf)
	{
		if(FILE_delete_file(dsfTextPath.c_str(),0) != 0)
		{
			//DoUserAlert(string("Could not remove temporary file " + dsfTextPath + ". You may delete this file if you wish").c_str());//TODO - is this not helpful to the user?
		}
	}
	if(FILE_delete_file(aptdatPath.c_str(),0) != 0)
	{
		//DoUserAlert(string("Could not remove temporary file " + aptdatPath + ". You may delete this file if you wish").c_str());//TODO - is this not helpful to the user?
	}
	if(FILE_delete_file(zipPath.c_str(),0) != 0)
	{
		//DoUserAlert(string("Could not remove temporary file " + zipPath + ". You may delete this file if you wish").c_str());//TODO - is this not helpful to the user?
	}
#endif
	return g;
}

void WED_GatewayImportDialog::DecorateGUIWindow(string labelDesc)
{
	/* Template for copy and pasting
	mBackButton->
	mBackButton->SetDescriptor("");

	mNextButton->
	mNextButton->SetDescriptor("");

	mLabel->
	mLabel->SetDescriptor("");
	*/
	switch(mPhase)
	{
	case imp_dialog_error:
		mBackButton->Hide();
		mBackButton->SetDescriptor("");

		mNextButton->Show();
		mNextButton->SetDescriptor("Exit");

		mLabel->Show();
		mLabel->SetDescriptor(labelDesc);
		
		mICAO_Packer->Hide();
		mVersions_Packer->Hide();
		break;
	case imp_dialog_download_ICAO:
		mBackButton->Show();
		mBackButton->SetDescriptor("Cancel");

		mNextButton->Hide();
		mNextButton->SetDescriptor("");
			
		mLabel->Show();
		mLabel->SetDescriptor(labelDesc);
		mICAO_Packer->Hide();
		mVersions_Packer->Hide();
		break;
	case imp_dialog_choose_ICAO:
		mBackButton->Show();
		mBackButton->SetDescriptor("Cancel");

		mNextButton->Show();
		mNextButton->SetDescriptor("Next");

		mLabel->Hide();
		mLabel->SetDescriptor(labelDesc);

		mICAO_Packer->Show();
		mVersions_Packer->Hide();
		break;
	case imp_dialog_download_versions:
		mBackButton->Show();
		mBackButton->SetDescriptor("Back");

		mNextButton->Hide();
		mNextButton->SetDescriptor("");

		mLabel->Show();
		mLabel->SetDescriptor(labelDesc);
		
		mICAO_Packer->Hide();
		mVersions_Packer->Hide();
		break;
	case imp_dialog_choose_versions:
		mFilter->ClearFilter();

		mBackButton->Show();
		mBackButton->SetDescriptor("Back");
			
		mNextButton->Show();
		mNextButton->SetDescriptor("Import Pack(s)");

		mLabel->Hide();
		mLabel->SetDescriptor(labelDesc);
			
		mICAO_Packer->Hide();
		mVersions_Packer->Show();
		
		break;
	case imp_dialog_download_specific_version:
	default:
		mBackButton->Hide();
		mBackButton->SetDescriptor("");

		mNextButton->Hide();
		mNextButton->SetDescriptor("");

		mLabel->Show();
		mLabel->SetDescriptor(labelDesc);
			
		mICAO_Packer->Hide();
		mVersions_Packer->Hide();
		break;
	}
}

void WED_GatewayImportDialog::MakeICAOTable(int bounds[4])
{
	mICAO_AptProvider.SetFilter(mFilter->GetText());//This requires mApts to be full
	
	mICAO_Scroller = new GUI_ScrollerPane(0,1);
	mICAO_Scroller->SetParent(mICAO_Packer);
	mICAO_Scroller->Show();
	mICAO_Scroller->SetSticky(1,1,1,1);

	mICAO_TextTable.SetProvider(&mICAO_AptProvider);
	mICAO_TextTable.SetGeometry(&mICAO_AptProvider);

	
	mICAO_TextTable.SetColors(
				WED_Color_RGBA(wed_Table_Gridlines),
				WED_Color_RGBA(wed_Table_Select),
				WED_Color_RGBA(wed_Table_Text),
				WED_Color_RGBA(wed_Table_SelectText),
				WED_Color_RGBA(wed_Table_Drag_Insert),
				WED_Color_RGBA(wed_Table_Drag_Into));
	mICAO_TextTable.SetTextFieldColors(
				WED_Color_RGBA(wed_TextField_Text),
				WED_Color_RGBA(wed_TextField_Hilite),
				WED_Color_RGBA(wed_TextField_Bkgnd),
				WED_Color_RGBA(wed_TextField_FocusRing));

	mICAO_Table = new GUI_Table(true);
	mICAO_Table->SetGeometry(&mICAO_AptProvider);
	mICAO_Table->SetContent(&mICAO_TextTable);
	mICAO_Table->SetParent(mICAO_Scroller);
	mICAO_Table->SetSticky(1,1,1,1);
	mICAO_Table->Show();	
	mICAO_Scroller->PositionInContentArea(mICAO_Table);
	mICAO_Scroller->SetContent(mICAO_Table);
	mICAO_TextTable.SetParentTable(mICAO_Table);

	mICAO_TextTableHeader.SetProvider(&mICAO_AptProvider);
	mICAO_TextTableHeader.SetGeometry(&mICAO_AptProvider);

	mICAO_TextTableHeader.SetImage("header.png");
	mICAO_TextTableHeader.SetColors(
			WED_Color_RGBA(wed_Table_Gridlines),
				WED_Color_RGBA(wed_Header_Text));

	mICAO_Header = new GUI_Header(true);

	bounds[1] = 0;
	bounds[3] = GUI_GetImageResourceHeight("header.png") / 2;
	mICAO_Header->SetBounds(bounds);
	mICAO_Header->SetGeometry(&mICAO_AptProvider);
	mICAO_Header->SetHeader(&mICAO_TextTableHeader);
	mICAO_Header->SetParent(mICAO_Packer);
	mICAO_Header->Show();
	mICAO_Header->SetSticky(1,0,1,1);
	mICAO_Header->SetTable(mICAO_Table);


					mICAO_TextTableHeader.AddListener(mICAO_Header);		// Header listens to text table to know when to refresh on col resize
					mICAO_TextTableHeader.AddListener(mICAO_Table);		// Table listense to text table header to announce scroll changes (and refresh) on col resize
					mICAO_TextTable.AddListener(mICAO_Table);				// Table listens to text table to know when content changes in a resizing way
					mICAO_AptProvider.AddListener(mICAO_Table);			// Table listens to actual property content to know when data itself changes

	//mICAO_Packer->PackPane(mFilter,gui_Pack_Top);
	mICAO_Packer->PackPane(mICAO_Header,gui_Pack_Top);

	mICAO_Packer->PackPane(mICAO_Scroller,gui_Pack_Center);
	mICAO_Scroller->PositionHeaderPane(mICAO_Header);
}

void WED_GatewayImportDialog::MakeVersionsTable(int bounds[4])
{
	mVersions_VerProvider.SetFilter(mFilter->GetText());//This requires mVers to be full?
	
	mVersions_Scroller = new GUI_ScrollerPane(1,1);
	mVersions_Scroller->SetParent(mVersions_Packer);
	
	mVersions_Scroller->Show();
	
	mVersions_Scroller->SetSticky(1,1,1,1);

	mVersions_TextTable.SetProvider(&mVersions_VerProvider);
	mVersions_TextTable.SetGeometry(&mVersions_VerProvider);

	
	mVersions_TextTable.SetColors(
				WED_Color_RGBA(wed_Table_Gridlines),
				WED_Color_RGBA(wed_Table_Select),
				WED_Color_RGBA(wed_Table_Text),
				WED_Color_RGBA(wed_Table_SelectText),
				WED_Color_RGBA(wed_Table_Drag_Insert),
				WED_Color_RGBA(wed_Table_Drag_Into));
	mVersions_TextTable.SetTextFieldColors(
				WED_Color_RGBA(wed_TextField_Text),
				WED_Color_RGBA(wed_TextField_Hilite),
				WED_Color_RGBA(wed_TextField_Bkgnd),
				WED_Color_RGBA(wed_TextField_FocusRing));

	mVersions_Table = new GUI_Table(true);
	mVersions_Table->SetGeometry(&mVersions_VerProvider);
	mVersions_Table->SetContent(&mVersions_TextTable);
	mVersions_Table->SetParent(mVersions_Scroller);
	mVersions_Table->SetSticky(1,1,1,1);
	mVersions_Table->Show();	
	mVersions_Scroller->PositionInContentArea(mVersions_Table);
	mVersions_Scroller->SetContent(mVersions_Table);
	mVersions_TextTable.SetParentTable(mVersions_Table);

	mVersions_TextTableHeader.SetProvider(&mVersions_VerProvider);
	mVersions_TextTableHeader.SetGeometry(&mVersions_VerProvider);

	mVersions_TextTableHeader.SetImage("header.png");
	mVersions_TextTableHeader.SetColors(
			WED_Color_RGBA(wed_Table_Gridlines),
				WED_Color_RGBA(wed_Header_Text));

	mVersions_Header = new GUI_Header(true);

	bounds[1] = 0;
	bounds[3] = GUI_GetImageResourceHeight("header.png") / 2;
	mVersions_Header->SetBounds(bounds);
	mVersions_Header->SetGeometry(&mVersions_VerProvider);
	mVersions_Header->SetHeader(&mVersions_TextTableHeader);
	mVersions_Header->SetParent(mVersions_Packer);
	
	//--IMPORTANT, gets shown with back and next---
	mVersions_Header->Show();
	//---------------------------------------------//

	mVersions_Header->SetSticky(1,0,1,1);
	mVersions_Header->SetTable(mVersions_Table);


					mVersions_TextTableHeader.AddListener(mVersions_Header);		// Header listens to text table to know when to refresh on col resize
					mVersions_TextTableHeader.AddListener(mVersions_Table);		// Table listense to text table header to announce scroll changes (and refresh) on col resize
					mVersions_TextTable.AddListener(mVersions_Table);				// Table listens to text table to know when content changes in a resizing way
					mVersions_VerProvider.AddListener(mVersions_Table);			// Table listens to actual property content to know when data itself changes

	mPacker->PackPane(mVersions_Header,gui_Pack_Top);
	mPacker->PackPane(mVersions_Scroller,gui_Pack_Center);
	mVersions_Scroller->PositionHeaderPane(mVersions_Header);
}
//-------------------------------------------------------------


//----------------------------------------------------------------------
int	WED_CanImportFromGateway(IResolver * resolver)
{
	return 1;
}

void WED_DoImportFromGateway(WED_Document * resolver, WED_MapPane * pane)
{
	new WED_GatewayImportDialog(resolver, pane,gApplication);
	return;
}

#endif /* HAS_GATEWAY */