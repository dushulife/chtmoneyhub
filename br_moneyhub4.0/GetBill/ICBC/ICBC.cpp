// CMBChinaGetBill.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <string>
#include <map>
#include <list>
#include <vector>
#include <atltime.h>
#include "../../BankData/BankData.h"
#include "../../ThirdParty/RunLog/RunLog.h"
#include "../../BankCore/WebBrowserEventsManager.h"
//#include "../../BankUI/UIControl/CoolMessageBox.h"

HWND g_notifyWnd = NULL;
int g_bState = 0;

#define CHECHSTATE if(g_bState != 0){state = g_bState;break;}

void WINAPI SetBillState(int State)
{
	g_bState = State;
}
void WINAPI SetNotifyWnd(HWND notifyWnd)
{
	g_notifyWnd = notifyWnd;
}

void ChangeNotifyWord(WCHAR* info)
{
	if(g_notifyWnd != 0)
		::PostMessage(g_notifyWnd, WM_BILL_CHANGE_NOFIFY, 0, (LPARAM)info);
}
void ShowNotifyWnd(bool bShow)
{
	if(g_notifyWnd != NULL)
	{
		if(!bShow)
			::SendMessage(g_notifyWnd, WM_BILL_HIDE_NOFIFY, 0, 0);
		else
			::SendMessage(g_notifyWnd, WM_BILL_SHOW_NOFIFY, 0, 0);
	}
}

wstring g_strDseID; // 会话ID

// 工行定义的货币值
enum EM_CURRENTCY_TYPE
{
	em_CType_Begin = 0,
	em_CType_RMB = 1, // 人民币
	em_CType_USD = 14,// 美元
	em_CType_End,
};

bool RecordInfo(wstring program, DWORD common, wchar_t *format, ...)
{
	wchar_t strTemp[MAX_INFO_LENGTH];
	memset(strTemp, 0, sizeof(strTemp));
	wchar_t *pTemp = strTemp;
	//合成信息
	va_list args; 
	va_start(args,format); 
	vswprintf(pTemp,MAX_INFO_LENGTH,format,args); 
	va_end(args); 

	wstring stemp(strTemp);


	wchar_t cinfo[20]= { 0 };
	swprintf(cinfo, 20, L"0x%08x", common);
	wstring wscTmp(cinfo);
	wscTmp  = program + L"-" + wscTmp + L"-" + stemp;

	CRunLog::GetInstance ()->GetLog ()->WriteSysLog (LOG_TYPE_INFO, L"%ws", wscTmp.c_str());
	return true;
}

CComVariant CallJScript(IHTMLDocument2 *doc, std::string strFunc,std::vector<std::string>& paramVec);
CComVariant CallJScript2(IHTMLDocument2 *doc, std::string strFunc, DISPPARAMS& dispparams);

string FilterStringNumber(string& scr)
{
	string result;
	char *p = (char*)scr.c_str();
	char temp[2] = { 0 };
	for(unsigned char i = 0;i < scr.size();i ++)
	{		
		if((((*p) >= '0')&&((*p) <= '9'))|| (*p) == '-' || (*p) =='.') 
		{
			memcpy(&temp[0], p, 1);
			result += temp; 
		}
		p ++;
	}
	return result;
}

void WINAPI FreeMemory(LPBILLRECORDS plRecords)
{
	list<LPBILLRECORD>::iterator ite = plRecords->BillRecordlist.begin();
	for(;ite != plRecords->BillRecordlist.end(); ite ++)
	{
		if((*ite) != NULL)
		{
			list<LPMONTHBILLRECORD>::iterator mite = (*ite)->bills.begin();
			for(;mite != (*ite)->bills.end(); mite ++)
			{
				list<LPTRANRECORD>::iterator lite = (*mite)->TranList.begin();
				for(; lite != (*mite)->TranList.end();lite ++)
				{
					if((*lite) != NULL)
						delete (*lite);
				}
				(*mite)->TranList.clear();
				delete (*mite);
			}
			(*ite)->bills.clear();

			delete (*ite);
		}
	}

	plRecords->BillRecordlist.clear();
	plRecords->m_mapBack.clear();
	memset(plRecords->aid, 0, 256);
	memset(plRecords->tag, 0, 256);
	plRecords->isFinish = false;

	g_strDseID.clear();
}



// 用strdst替换字符串中所有能和strsrc匹配的字符串
void ReplaceCharInString(std::string& strBig, const std::string & strsrc, const std::string &strdst)  
{  
    std::string::size_type pos = 0;
     while( (pos = strBig.find(strsrc, pos)) != string::npos)
     {
         strBig.replace(pos, strsrc.length(), strdst);
         pos += strdst.length();
     }
}

// 通过框架名称，得到子框架的document2指针
bool ExeScriptByFramesName(IWebBrowser2* pWebBrowser, std::vector<std::wstring>& frameVec, std::string strFunc,std::vector<std::string>& paramVec)
{
	if(!pWebBrowser)
		return false;
	bool bResult = false;

	IHTMLDocument2* pDoc2 = NULL;
	CComPtr<IDispatch> pDispatch;
	if (SUCCEEDED(pWebBrowser->get_Document(&pDispatch)))
	{
		if (SUCCEEDED(pDispatch->QueryInterface(IID_IHTMLDocument2, (void**)&pDoc2)))
		{
			// 按照框架名称一层一层找下去，找到最后一个就能得到一个文档
			std::vector<std::wstring>::const_iterator itPointer;

			for(itPointer = frameVec.begin(); itPointer != frameVec.end(); itPointer ++)
			{
				bResult = false;
				CComPtr<IHTMLFramesCollection2> pFrame = NULL;
				if(!SUCCEEDED(pDoc2->get_frames(&pFrame)))
					break;

				VARIANT sPram;
				sPram.vt = VT_BSTR;
				sPram.bstrVal = (BSTR)(*itPointer).c_str();

				VARIANT frameOut;//找到iFrame对象
				if(!SUCCEEDED(pFrame->item(&sPram, &frameOut)))
				{
					break;
				}

				CComPtr<IHTMLWindow2> pRightFrameWindow = NULL;
				if (!SUCCEEDED(V_DISPATCH(&frameOut)->QueryInterface(IID_IHTMLWindow2,
					(void**)&pRightFrameWindow)))
				{
					break;
				}

				if(pDoc2 != NULL){pDoc2->Release();pDoc2 = NULL;}

				if (SUCCEEDED(pRightFrameWindow->get_document(&pDoc2)))
				{
					bResult = true;
					continue;
				}

				if(pDoc2 == NULL)
					break;
			}

			if(pDoc2 != NULL && bResult == true)
			{
				RecordInfo(L"ICBC", 1800, L"CallJScript 选中我的账户");
				CallJScript(pDoc2, strFunc, paramVec);
				bResult = true;
			}
			else
				bResult = false;

		}

	}
	if(pDoc2 != NULL){pDoc2->Release();pDoc2 = NULL;}

	return bResult;
}

int TransferDataWithServe(const wstring& strSessionID, const wstring& wstrParam, wstring& revinfo, int type = 0)
{
	HINTERNET		m_hInetSession; // 会话句柄
	HINTERNET		m_hInetConnection; // 连接句柄
	HINTERNET		m_hInetFile; //
	HANDLE			hSaveFile;

	m_hInetSession = ::InternetOpen(L"Mozilla/5.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (m_hInetSession == NULL)
	{
		return 3000;
	}
	
	DWORD dwTimeOut = 60000;
	InternetSetOptionEx(m_hInetSession, INTERNET_OPTION_CONTROL_RECEIVE_TIMEOUT, &dwTimeOut, sizeof(DWORD), 0);
	InternetSetOptionEx(m_hInetSession, INTERNET_OPTION_CONTROL_SEND_TIMEOUT, &dwTimeOut, sizeof(DWORD), 0);
	InternetSetOptionEx(m_hInetSession, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeOut, sizeof(DWORD), 0);
	InternetSetOptionEx(m_hInetSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeOut, sizeof(DWORD), 0);
	InternetSetOptionEx(m_hInetSession, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeOut, sizeof(DWORD), 0);
	

	m_hInetConnection = ::InternetConnect(m_hInetSession, L"mybank.icbc.com.cn", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
	if (m_hInetConnection == NULL)
	{
		InternetCloseHandle(m_hInetSession);

		return 3001;
	}

	LPCTSTR ppszAcceptTypes[2];
	ppszAcceptTypes[0] = _T("*/*"); 
	ppszAcceptTypes[1] = NULL;
	
	USES_CONVERSION;
	WCHAR oName[2048] = {0};
	
	swprintf_s(oName, 2048 , L"%s", wstrParam.c_str());
	
	m_hInetFile = HttpOpenRequestW(m_hInetConnection, _T("GET"), oName, NULL, NULL, ppszAcceptTypes, INTERNET_FLAG_SECURE /*| INTERNET_FLAG_DONT_CACHE*/ | INTERNET_FLAG_KEEP_CONNECTION, 0);
	if (m_hInetFile == NULL)
	{
		InternetCloseHandle(m_hInetConnection);
		InternetCloseHandle(m_hInetSession);
		return 3002;
	}	

	TCHAR szHeaders[1024];	
	_stprintf_s(szHeaders, _countof(szHeaders), _T("Cache-Control: no-cache\
		Accept:text/html,application/xhtml+xml,*/*\
		Accept-Encoding:gzip, deflateAccept-Language:zh-cn\
		User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0)\
		Cookie: JESSIONID=%s;-1\
		Referer: https://mybank.icbc.com.cn/icbc/newperbank/card/intercard_querysub1.jsp?dse_sessionId=%s\
		Content-Length: 251\
		Content-Type: application/x-www-form-urlencoded\
		Connection: Keep-Alive\
		Host: mybank.icbc.com.cn"), strSessionID.c_str(), strSessionID.c_str());

	BOOL ret = HttpAddRequestHeaders(m_hInetFile, szHeaders, -1, HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
	

	BOOL bSend = ::HttpSendRequestW(m_hInetFile, NULL, 0, NULL, 0);
	if (!bSend)
	{
		int err = GetLastError();
		InternetCloseHandle(m_hInetConnection);
		InternetCloseHandle(m_hInetFile);
		InternetCloseHandle(m_hInetSession);

		return err;
	}
	
	TCHAR szStatusCode[32];
	DWORD dwInfoSize = sizeof(szStatusCode);
	if (!HttpQueryInfo(m_hInetFile, HTTP_QUERY_STATUS_CODE, szStatusCode, &dwInfoSize, NULL))
	{
		InternetCloseHandle(m_hInetConnection);
		InternetCloseHandle(m_hInetFile);
		InternetCloseHandle(m_hInetSession);

		return 3004;
	}
	else
	{
		long nStatusCode = _ttol(szStatusCode);
		if (nStatusCode != HTTP_STATUS_PARTIAL_CONTENT && nStatusCode != HTTP_STATUS_OK)
		{
			InternetCloseHandle(m_hInetConnection);
			InternetCloseHandle(m_hInetFile);
			InternetCloseHandle(m_hInetSession);
			return 3005;
		}
	}


	if(type == 0)
	{
		TCHAR szContentLength[32] = {0};
		dwInfoSize = sizeof(szContentLength);
		if (!::HttpQueryInfo(m_hInetFile, HTTP_QUERY_CONTENT_LENGTH, szContentLength, &dwInfoSize, NULL))
		{
			InternetCloseHandle(m_hInetConnection);
			InternetCloseHandle(m_hInetFile);
			InternetCloseHandle(m_hInetSession);

			return 3006;
		}
		DWORD revSize = 0;
		if(wcslen(szContentLength) != 0)
			revSize = _wtol(szContentLength);

		DWORD dwBytesRead = 0;
		char szReadBuf[1024];
		char* pBuf = new char[revSize + 1];//大小的内存
		if(pBuf == NULL)
		{
			InternetCloseHandle(m_hInetConnection);
			InternetCloseHandle(m_hInetFile);
			InternetCloseHandle(m_hInetSession);
			return 1021;
		}
		memset(pBuf, 0, revSize + 1);
		char* pCur = pBuf;
		DWORD dwBytesToRead = sizeof(szReadBuf);
		bool sucess = true;//这里需要将读到的数据进行拼装

		do
		{
			if (!::InternetReadFile(m_hInetFile, szReadBuf, dwBytesToRead, &dwBytesRead))
			{
				sucess = false;
				break;
			}
			else if (dwBytesRead)
			{
				memcpy(pCur , szReadBuf, dwBytesRead);
				pCur += dwBytesRead;
			}
		}while (dwBytesRead);

		int textlen;
		wchar_t * pResult;
		textlen = MultiByteToWideChar( CP_UTF8, 0, pBuf, -1, NULL, 0 ); 
		pResult = (wchar_t *)malloc((textlen + 1)*sizeof(wchar_t)); 
		memset(pResult, 0, (textlen + 1)*sizeof(wchar_t)); 
		MultiByteToWideChar(CP_UTF8, 0,pBuf,-1,(LPWSTR)pResult,textlen ); 


		//招行返回值为utf-8格式的数据
		revinfo = pResult;
		delete[] pResult;
		delete[] pBuf;

		InternetCloseHandle(m_hInetConnection);
		InternetCloseHandle(m_hInetFile);
		InternetCloseHandle(m_hInetSession);

		if(sucess == false)
		{
			revinfo = L"";
			return 1020;
		}
		return 0;
	}
	else
	{
		DWORD dwBytesRead = 0;

		WCHAR szAppDataFileName[MAX_PATH + 1];
		ExpandEnvironmentStringsW(L"%APPDATA%\\MoneyHub\\icbc.txt", szAppDataFileName, MAX_PATH);

		LPBYTE szReadBuf = NULL;
		DWORD dwBytesToRead = 0;

		hSaveFile = CreateFile(szAppDataFileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hSaveFile == INVALID_HANDLE_VALUE)
		{
			int err = GetLastError();
			return err;
		}
		do
		{
			if (!InternetQueryDataAvailable(m_hInetFile, &dwBytesToRead, 0, 0))   
				break;   

			if (dwBytesToRead <= 0)
				break;

			szReadBuf = new BYTE[dwBytesToRead];   
			//DWORD dwDownloaded = 0;

			if (!::InternetReadFile(m_hInetFile, szReadBuf, dwBytesToRead, &dwBytesRead))
			{
				delete[] szReadBuf;
				break;
			}
			else if (dwBytesRead)
			{

				DWORD dwBytesWritten = 0;
				BOOL bWrite = WriteFile(hSaveFile, szReadBuf, dwBytesRead, &dwBytesWritten, NULL);

				delete[] szReadBuf;
				szReadBuf = NULL;

				if (!bWrite)
				{
					break;
				}
			}
		}while (dwBytesRead);
		CloseHandle(hSaveFile);

		InternetCloseHandle(m_hInetConnection);
		InternetCloseHandle(m_hInetFile);
		InternetCloseHandle(m_hInetSession);

		return 0;
	}
}


#define MY_DATE_LENGTH 10
//2011-06-01
bool MyDateCompare(const char* pSour)
{
	ATLASSERT (NULL != pSour);
	if (NULL == pSour)
		return false;

	if(strlen(pSour) < 10)
		return false;

	for (int i = 0; i < 10; i ++)
	{
		if (i == 4 || i == 7)
		{
			if (*(pSour + i) != '-')
				return false;
		}
		else
		{
			if (*(pSour + i) < '0' || *(pSour + i) > '9')
				return false;
		}
	}
	return true;
}

bool ParseFileData(const char* pBuffer, const DWORD& dwSize, LPMONTHBILLRECORD pMonthRecord)
{
	ATLASSERT (NULL != pBuffer && dwSize > 0 && pMonthRecord != NULL);
	if (NULL == pBuffer || dwSize < 0 || pMonthRecord == NULL)
		return false;

	unsigned int i = 0;

	// 第一日期的处理和其它的有点不同，考虑到效率，所有将第一日期单独处理
	for (; i < dwSize - MY_DATE_LENGTH - 1; i ++)
	{
		if (MyDateCompare (pBuffer + i))
		{
			i += MY_DATE_LENGTH;
			break;
		}
	}

	int nTimes = 1;
	vector<string> vectorTemp;
	string strTp;
	for (; i < dwSize - MY_DATE_LENGTH - 1; i ++)
	{
		if (MyDateCompare (pBuffer + i))
		{
			nTimes ++;

			if (nTimes % 2 == 0) // 奇数后后面的值不是我们要的，偶数后面才是
			{
				vectorTemp.clear();
				for (; i < dwSize - MY_DATE_LENGTH - 1; i ++)
				{
					// 第一项值以'^'符号分隔出来
					if (*(pBuffer + i) == '^')
					{
						vectorTemp.push_back(strTp);
						strTp.clear();

					}
					else
					{
						// 去掉空格
						if (*(pBuffer + i) != ' ')
							strTp += *(pBuffer + i);
					}

					//  校验下一个是不是日期标志
					if (MyDateCompare (pBuffer + i + 1))
					{
						if(vectorTemp.size() < 5)
							break;
						ReplaceCharInString(vectorTemp[0], "-", "");
						// 记录一条交易记录
						LPTRANRECORD pTransRecord = new TRANRECORD;
						memcpy (pTransRecord->TransDate, vectorTemp[0].c_str(), vectorTemp[0].size());// 交易日期
						memcpy (pTransRecord->PostDate, vectorTemp[0].c_str(), vectorTemp[0].size());// 交易日期
						memcpy (pTransRecord->Payee, vectorTemp[5].c_str(), vectorTemp[5].size()); // 交易摘要
						 
						if (vectorTemp[2].size() > 0)// 交易金额
						{
							ReplaceCharInString(vectorTemp[2], ",", "");
							string strTpIn; // 收入为负
							strTpIn = "-";
							strTpIn  += vectorTemp[2];
							memcpy (pTransRecord->Amount, strTpIn.c_str(), strTpIn.size());
						}
						else
						{
							ReplaceCharInString(vectorTemp[3], ",", "");
							memcpy (pTransRecord->Amount, vectorTemp[3].c_str(), vectorTemp[3].size());
						}

						memcpy (pTransRecord->Country, vectorTemp[5].c_str(), vectorTemp[5].size()); // 交易地点

						pMonthRecord->TranList.push_back(pTransRecord);
						
						nTimes ++;
						break;
					}
				}
				
			}
			
			i += MY_DATE_LENGTH; // 每个日期的长度为10
		}
	}

	return true;
}

// 得到指定文件的大小和内容
char *GetTempFileTextBuffer(LPCTSTR lpPath, DWORD& dwSize)
{
	ATLASSERT (NULL != lpPath);
	if (NULL == lpPath)
		return NULL;

	LPBYTE szReadBuf = NULL;
	DWORD dwBytesToRead = 0;

	HANDLE hSaveFile = CreateFile(lpPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hSaveFile == INVALID_HANDLE_VALUE)
	{
		return NULL;
	}

	dwSize = GetFileSize(hSaveFile, NULL);
	if (0 >= dwSize)
	{
		CloseHandle(hSaveFile);
		return NULL;
	}

	char* pBuffer = new char[dwSize + 1];
	ATLASSERT(NULL != pBuffer);
	memset(pBuffer, 0, dwSize + 1);

	DWORD dwRead = 0;
	BOOL bBack = ::ReadFile(hSaveFile, (LPVOID)(LPCTSTR)pBuffer, dwSize, &dwRead, NULL);
	
	CloseHandle(hSaveFile);
	
	if (!bBack || dwRead != dwSize)
	{
		delete[] pBuffer;
		return NULL;
	}

	return pBuffer;
}

bool ReadAllBillRecords(LPMONTHBILLRECORD pMonthRecord)
{
	WCHAR szAppDataFileName[MAX_PATH + 1];
	ExpandEnvironmentStringsW(L"%APPDATA%\\MoneyHub\\icbc.txt", szAppDataFileName, MAX_PATH);

	DWORD dwSize = 0;
	char* pBuffer = GetTempFileTextBuffer(szAppDataFileName, dwSize);
	if (NULL == pBuffer)
		return false;

	ParseFileData(pBuffer, dwSize, pMonthRecord);

	delete[] pBuffer;
	DeleteFile (szAppDataFileName); // 删掉临时文件
	return true;
}

//读取余额（从银行获取的数据如：{"sumBalance":"-5828112","currType":"人民币","cashSign":"钞"}）
/*{"fdravailBalance":"00000000000000000",
"fbalance":"00000000000000000",
"favailBalance":"00000000000150000",
"fdrbalance":"00000000000000000",
"fcurrType":"美元",？表明该账号有美元和人民币两个账号？ 该账号是
"balance":"-1800",
"frostBalance":"",
"drbalance":"00000000000000000",
"rg":[{"sumBalance":"-1800","currType":"人民币","cashSign":"钞"},{"sumBalance":"00000000000000000","currType":"美元","cashSign":"钞"}],"availBalance":"00000000007498200",
}
3账号的

{"fdravailBalance":"00000000000000000",
"fbalance":"00000000000000000",
"favailBalance":"00000000000000000",
"fdrbalance":"00000000000000000",
"fcurrType":"000",//这个是只有人民币账户的
"balance":"00000000000000000",//该账户的人民币余额
"frostBalance":"",
"drbalance":"00000000000000000",//该账户的美元余额

"rg":[{"sumBalance":"-1800","currType":"人民币","cashSign":"钞"},{"sumBalance":"00000000000000000","currType":"美元","cashSign":"钞"}],
}
6账号的*/
bool ReadSumBalanceValue(LPBILLRECORDS plRecords, wstring& revinfo)
{
	LPBILLRECORD prRecord = new BILLRECORD;
	prRecord->type = RMB;
	prRecord->balance = "F";
	plRecords->BillRecordlist.push_back(prRecord);

	LPBILLRECORD puRecord = new BILLRECORD;
	puRecord->type = USD;
	puRecord->balance = "F";
	plRecords->BillRecordlist.push_back(puRecord);

	if (revinfo.size() <= 0)
		return false;

	size_t nRmbbegin = revinfo.find(L"\"balance\":");

	if(nRmbbegin == wstring::npos)
		return false;

	size_t nRmbend = revinfo.find(L"\",", nRmbbegin);

	if(nRmbend == wstring::npos)
		return false;

	wstring rmbbalance = revinfo.substr(nRmbbegin + 11, nRmbend - nRmbbegin - 11);
	rmbbalance.insert(rmbbalance.length() - 2, L".");

	USES_CONVERSION;
	string srmbba = W2A(rmbbalance.c_str());
	string rsrmbba = FilterStringNumber(srmbba);

	if(rsrmbba.size() < 3)
		return false;

	float frbalance = atof(rsrmbba.c_str());
	char sfrbalance[256] = {0};
	sprintf_s(sfrbalance, 256, "%.2f", frbalance);

	prRecord->balance = sfrbalance;
	

	size_t nUsdbegin = revinfo.find(L"\"drbalance\":");

	if(nUsdbegin == wstring::npos)
		return false;

	size_t nUsdend = revinfo.find(L"\",", nUsdbegin);

	if(nUsdend == wstring::npos)
		return false;

	wstring usdbalance = revinfo.substr(nUsdbegin + 13, nUsdend - nUsdbegin - 13);
	usdbalance.insert(usdbalance.length() - 2, L".");

	string susdba = W2A(usdbalance.c_str());
	string rusdba = FilterStringNumber(susdba);

	if(rusdba.size() < 3)
		return false;

	float fubalance = atof(rusdba.c_str());
	char sfubalance[256] = {0};
	sprintf_s(sfubalance, 256, "%.2f", fubalance);

	size_t nBegin = revinfo.find(L"\"fcurrType\"");
	if(nBegin == wstring::npos)
		return false;

	size_t nTBegin = revinfo.find(L":\"", nBegin + 5);
	if(nTBegin == wstring::npos)
		return false;

	size_t nTEnd = revinfo.find(L"\",", nTBegin + 2);
	if(nTEnd == wstring::npos)
		return false;

	wstring type =  revinfo.substr(nTBegin + 2, nTEnd - nTBegin - 2);
	//if(type == L"美元")//说明是双币种用户
	if(type != L"000")//只有人民币账户的，这个是用于交管局用的罚款卡，不知道其他类型是不是也支持
	{
		puRecord->balance = sfubalance;
	}
	else
	{
		//删除usd账户的
		list<LPBILLRECORD>::iterator ite = std::find(plRecords->BillRecordlist.begin(), plRecords->BillRecordlist.end(), puRecord);
		if(ite != plRecords->BillRecordlist.end())
		{
			plRecords->BillRecordlist.erase(ite);
			delete puRecord;
		}
	}
		

	return true;
}


// 导入各种类型货币的账单
void ImportAllCurrencyTypeBill(const string& strAccountID, const LPCTSTR lpBeginTime,
							   const LPCTSTR lpEndTime, LPBILLRECORDS plRecords, int& state)
{
	// 卡号
	wstring cardNum = CA2W(strAccountID.c_str());

	// 当前时间
	CString strTime1, strTime2, cstrParam;
	wstring strParam;
	CTime time;
	time = CTime::GetCurrentTime();
	strTime1.Format(L"%d:%02d:%02d", time.GetYear(), time.GetMonth(), time.GetDay());
	strTime2.Format(L"%d:%02d:%02d", time.GetHour(), time.GetMinute(), time.GetSecond());

	LPBILLRECORD pRecord = NULL;
	list<LPBILLRECORD> ::iterator pite = plRecords->BillRecordlist.begin();
	//找到已经生成的人民币账单和美元账单地址
	for(;pite != plRecords->BillRecordlist.end(); pite ++)
	{
		pRecord = (*pite);
		int gparam = 1;
		if(pRecord->type == RMB)
		{
			gparam = 1;
		}
		else
			gparam = 14;

		CString strCurrType;
		strCurrType.Format(L"%03d", gparam);

		// 拼接出发送给银行服务器端的数据
		//GET /servlet/ICBCINBSReqServlet?dse_sessionId=d2hGBbqZy3zskmJDkhV4Q4p&head_top_num=1&qaccf=0&FovaAcctType=0&Area=&cardNum=370246016512965&cardType=007&acctCode=&acctNum=&begDate=2011-09-26&endDate=2011-10-26&Begin_pos=&acctIndex=4&Tran_flag=1&acctType=&queryType=3&cardAlias=&acctAlias=&acctTypeName=&currTypeName=&currType=014&cityFlag=0&drcrFlag=0&acctSelList2=no&Areacode=0200&init_flag=1&pageflag=0&initDate=2011:10:26&initTime=15:10:15&dse_operationName=per_AccountQueryHisdetailOp&dse_pageId=&dse_parentContextName=&dse_processorState=&dse_processorId= HTTP/1.1
		//GET /servlet/ICBCINBSReqServlet?dse_sessionId=d2hGBbqZy3zskmJDkhV4Q4p&head_top_num=1&qaccf=0&FovaAcctType=0&Area=&cardNum=370246016512965&cardType=007&acctCode=&acctNum=&begDate=2011-09-26&endDate=2011-10-26&Begin_pos=&acctIndex=4&Tran_flag=1&acctType=&queryType=3&cardAlias=&acctAlias=&acctTypeName=&currTypeName=&currType=001&cityFlag=0&drcrFlag=0&acctSelList2=no&Areacode=0200&init_flag=1&pageflag=0&initDate=2011:10:26&initTime=15:07:03&dse_operationName=per_AccountQueryHisdetailOp&dse_pageId=&dse_parentContextName=&dse_processorState=&dse_processorId= HTTP/1.1
		cstrParam.Format(L"/servlet/ICBCINBSReqServlet?dse_sessionId=%s&head_top_num=1&qaccf=0&FovaAcctType=0&Area=&cardNum=%s&cardType=007\
						  &acctCode=&acctNum=&begDate=%s&endDate=%s&Begin_pos=&acctIndex=4&Tran_flag=1&acctType=&queryType=3&cardAlias=&acctAlias=&acctTypeName=\
						  &currTypeName=&currType=%s&cityFlag=0&drcrFlag=0&acctSelList2=no&Areacode=0200&init_flag=1&pageflag=0&initDate=%s&initTime=%s\
						  &dse_operationName=per_AccountQueryHisdetailOp&dse_pageId=&dse_parentContextName=&dse_processorState=&dse_processorId= HTTP/1.1",
						  g_strDseID.c_str(), cardNum.c_str(), lpBeginTime, lpEndTime, strCurrType, strTime1, strTime2);

		strParam = cstrParam;
		LPMONTHBILLRECORD pMonthRecord = new MONTHBILLRECORD;
		pMonthRecord->m_isSuccess = false;
		// 记录月份
		string strMonth = CW2A(lpBeginTime);
		strMonth += CW2A(lpEndTime);
		ReplaceCharInString(strMonth, "-", "");
		pMonthRecord->month = strMonth;

		pRecord->bills.push_back(pMonthRecord); // 账单记录

		wstring revinfo;
		if (0 == TransferDataWithServe(g_strDseID, strParam, revinfo, 1))
		{
			CHECHSTATE;
			// 读取账单数据
			if (ReadAllBillRecords(pMonthRecord))
				pMonthRecord->m_isSuccess = true;
		}
	}
}

int WINAPI FetchBillFunc(IWebBrowser2* pFatherWebBrowser, IWebBrowser2* pChildWebBrowser, BillData* pData, int &step, LPBILLRECORDS plRecords)
{
	USES_CONVERSION;
	int state = 0;
	if(pData->aid == "a003")
	{
		RecordInfo(L"ICBCDll", 1800, L"进入导入账单处理%d", step);
		HRESULT hr = S_OK;
		IDispatch *docDisp = NULL;
		IHTMLDocument2 *doc2 = NULL;
		IHTMLDocument3 *doc3 = NULL;
		IHTMLElement *elem	= NULL;


		hr = pFatherWebBrowser->get_Document(&docDisp);
		if (SUCCEEDED(hr) && docDisp != NULL)
		{					
			hr = docDisp->QueryInterface(IID_IHTMLDocument2, reinterpret_cast<void**>(&doc2));	
			if (SUCCEEDED(hr) || doc2 != NULL) 
			{
				if (docDisp) { docDisp->Release(); docDisp = NULL; }

				switch(step)
				{					
				case 1:
					{
						IHTMLDocument3* pDoc3 = NULL;
						CComPtr<IHTMLFramesCollection2> pFrame = NULL;
						if(!SUCCEEDED(doc2->get_frames(&pFrame)))
							break;

						VARIANT sPram;
						sPram.vt = VT_BSTR;
						sPram.bstrVal = (BSTR)L"indexFrame";

						VARIANT frameOut;//找到iFrame对象
						if(SUCCEEDED(pFrame->item(&sPram, &frameOut)))
						{
							CComPtr<IHTMLWindow2> pRightFrameWindow = NULL;
							if (!SUCCEEDED(V_DISPATCH(&frameOut)->QueryInterface(IID_IHTMLWindow2,
								(void**)&pRightFrameWindow)))
								break;

							CComPtr<IHTMLDocument2> pDocument2 = NULL;
							CComPtr<IHTMLDocument3> pDocument3 = NULL;
							if(SUCCEEDED(pRightFrameWindow->get_document(&pDocument2)))
							{
								if (SUCCEEDED(pDocument2->QueryInterface(IID_IHTMLDocument3, reinterpret_cast<void**>(&pDocument3))))
								{
									CComPtr<IHTMLElement> pElement = NULL;
									if(SUCCEEDED(pDocument3->getElementById(L"sel0", &pElement)))
									{
										pElement->click();
									}
								}
							}					
								

						}	
						
					}

					break;
				case 20:
					{
						BSTR pbUrl = NULL;
						pChildWebBrowser->get_LocationURL(&pbUrl);
						if(NULL != pbUrl)
						{
							wstring url = pbUrl;
							if(url.find(L"/servlet/com.icbc.inbs.servlet.ICBCINBSEstablishSessionServlet") != wstring::npos)
							{
								IDispatch *docDisp_c			= NULL;
								IHTMLDocument2 *doc_c			= NULL;

								hr = pChildWebBrowser->get_Document(&docDisp_c);
								if (SUCCEEDED(hr) && docDisp_c != NULL)
								{					
									hr = docDisp_c->QueryInterface(IID_IHTMLDocument2, reinterpret_cast<void**>(&doc_c));
									if (SUCCEEDED(hr) && doc_c != NULL) 
									{
										if (docDisp_c) { docDisp_c->Release(); docDisp_c = NULL; }
									}
								}
								
								hr = doc_c->get_body( &elem);

								wstring info;
								if(elem!=NULL)   
								{      
									BSTR pbBody = NULL;
									elem->get_innerHTML(&pbBody);   //类似的还有put_innerTEXT
									if(pbBody != NULL)
										info = pbBody;
									elem->Release();   
									elem = NULL;
								}
								if (doc_c) { doc_c->Release(); doc_c = NULL; }

								size_t nPos = info.find(L"继续登录");
								if(nPos != wstring::npos)
								{							
									wstring SessionId;
									size_t spos = info.find(L"dse_sessionId=");
									if(spos != wstring::npos)
									{
										size_t epos = info.find(L"&", spos);
										if(epos != wstring::npos)
											SessionId = info.substr(spos, epos - spos);
									}
									if(SessionId != L"")
									{
										wstring nUrl = L"https://mybank.icbc.com.cn/icbc/newperbank/includes/frameset.jsp?" + SessionId + L"&vipContinue=1";
										CComVariant var;
										pChildWebBrowser->Navigate((BSTR)(LPCTSTR)nUrl.c_str(), &var, &var, &var, &var);
										step = 2;
									}

								}
								else
									step = 1;
							}
						}
					}

					break;

				case 2: // 普通用户入口
					{
						BSTR pbUrl = NULL;
						pChildWebBrowser->get_LocationURL(&pbUrl);
						if(NULL != pbUrl)
						{
							wstring url = pbUrl;//加载完成
							if(url.find(L"/icbc/newperbank/includes/frameset.jsp?") != wstring::npos)
							{								
								size_t 	npos= url.find(L"dse_sessionId=");
								if(npos != wstring::npos)
								{
									size_t epos = url.find(L"&", npos);
									if(epos != wstring::npos)
										g_strDseID = url.substr(npos + 14, epos - npos - 14);
									else
										g_strDseID = url.substr(npos + 14);

								}
								if(g_strDseID == L"")
								{
									state = BILL_COM_ERROR;
									break;
								}
								//这里需要点击执行函数，用Get费劲。得到账号
								RecordInfo(L"ICBC", 1800, L"ICBC 345");
								std::vector<std::wstring> frameVec;
								frameVec.push_back(L"indexFrame");
								frameVec.push_back(L"topFrame");
								std::vector<std::string> paramVec;
								paramVec.push_back("1");
								paramVec.push_back("02");

								
								bool bResult = ExeScriptByFramesName(pFatherWebBrowser, frameVec, "onSubtopForm", paramVec);
								RecordInfo(L"ICBC", 1800, L"CallJScript 选中我的账户");

								step = 4;
							}
						}

					}
					break;
				case 4:
					{
						BSTR pbUrl = NULL;
						pChildWebBrowser->get_LocationURL(&pbUrl);
						if(NULL == pbUrl)
							break;

						wstring url = pbUrl;
						if(url.find(L"/servlet/com.icbc.inbs.person.servlet.ICBCINBSCenterServlet?") == wstring::npos)
							break;

						IDispatch *docDisp_c			= NULL;
						IHTMLDocument2 *doc_c			= NULL;

						hr = pChildWebBrowser->get_Document(&docDisp_c);
						if (SUCCEEDED(hr) && docDisp_c != NULL)
						{					
							hr = docDisp_c->QueryInterface(IID_IHTMLDocument2, reinterpret_cast<void**>(&doc_c));
							if (SUCCEEDED(hr) && doc_c != NULL) 
							{
								if (docDisp_c) { docDisp_c->Release(); docDisp_c = NULL; }
							}
						}
						//跳转到了查询标签页,获得账号末4位
						hr = doc_c->get_body( &elem);

						wstring info;
						if(elem!=NULL)   
						{      
							BSTR pbBody = NULL;
							elem->get_innerHTML(&pbBody);   //类似的还有put_innerTEXT
							if(pbBody != NULL)
								info = pbBody;
							elem->Release();   
							elem = NULL;
						}
						if (doc_c) { doc_c->Release(); doc_c = NULL; }

						size_t curpos = 0;
						
						list<wstring> accountlist;

						for(;curpos != wstring::npos;)
						{
							size_t npos = info.find(L"贷记卡", curpos);
							if(npos != wstring::npos)
							{
								curpos = npos + 3;
								size_t bpos = info.find(L"id=span_blance_", npos);

								if(bpos != wstring::npos)
								{
									size_t epos = info.find(L"_0", bpos);
									if(epos != wstring::npos)
									{
										wstring ac = info.substr(bpos + 15, epos - bpos - 15);
										accountlist.push_back(ac);
									}
								}
							}
							else
								break;
						}

						if(accountlist.size() <= 0)
						{
							state = BILL_GET_ACCOUNT_ERROR;
							break;							
						}

						bool isGetAccount = false;
						wstring wtag = A2W(pData->tag.c_str());
						list<wstring>::iterator ite = accountlist.begin();
						for(;ite != accountlist.end(); ite ++)
						{
							wstring tag = (*ite).substr((*ite).size() - 4, 4);
							if(pData->tag != "")
							{								
								if(tag == wtag)
								{
									isGetAccount = true;//多账号的问题
									break;
								}
							}
						}
						SYSTEMTIME time;//获得2年前的时间//工行目前只能获得2年的账单
						GetLocalTime(&time);
						if(time.wMonth == 2 && time.wDay == 29) //闰年的情况改为查询3月1日的
						{ 
							time.wMonth = 3;
							time.wDay = 1; 
						}
						time.wYear = time.wYear - 2;
						
						if(isGetAccount == true)//说明在这些选择中已经找到了要找的账号，下面直接选择月份
						{
							SELECTINFONODE selectNode;
							string sNode = W2A((*ite).c_str());
							strcpy_s(selectNode.szNodeInfo, 256, sNode.c_str());
							selectNode.dwVal = CHECKBOX_SHOW_CHECKED;
							plRecords->m_mapBack.push_back(selectNode);

							sprintf_s(pData->select ,256,"%d-%d-%d 0:00:00", time.wYear, time.wMonth, time.wDay);
							state = BILL_SELECT_MONTH2;
						}
						else
						{	
							if(pData->tag != "")
							{
								ShowNotifyWnd(false);//隐藏正在登陆框
								WCHAR sInfo[256] = { 0 };
								swprintf(sInfo, 256, L"当前账户已绑定工行信用卡卡号末4位%s，实际导入工行卡号与原账户不一致，是否继续导入",A2W(pData->tag.c_str()));

								HWND hMainFrame = FindWindow(_T("MONEYHUB_MAINFRAME"), NULL);
								if(MessageBoxW(hMainFrame, sInfo, L"财金汇", MB_OKCANCEL) == IDCANCEL)
								{
									state = BLII_NEED_RESTART;
									break;
								}

								ShowNotifyWnd(true);//显示正在登陆框
							}
						

							if(accountlist.size() == 1) //选择月份
							{
								SELECTINFONODE selectNode;
								string sNode = W2A((*ite).c_str());
								strcpy_s(selectNode.szNodeInfo, 256, sNode.c_str());
								selectNode.dwVal = CHECKBOX_SHOW_CHECKED;
								plRecords->m_mapBack.push_back(selectNode);

								sprintf_s(pData->select ,256,"%d-%d-%d 0:00:00", time.wYear, time.wMonth, time.wDay);
								state = BILL_SELECT_MONTH2;
							}
							else//选账号，选月份
							{	
								plRecords->m_mapBack.clear();
								ite = accountlist.begin();
								for(;ite != accountlist.end(); ite ++)
								{

									SELECTINFONODE selectNode;
									string sNode = W2A((*ite).c_str());
									strcpy_s(selectNode.szNodeInfo, 256, sNode.c_str());
									selectNode.dwVal = CHECKBOX_SHOW_UNCHECKED;
									plRecords->m_mapBack.push_back(selectNode);
								}

								sprintf_s(pData->select ,256,"%d-%d-%d 0:00:00", time.wYear, time.wMonth, time.wDay);
								RecordInfo(L"ICBC", 1800, L"即将弹出选账号和选月份的界面");
								state = BILL_SELECT_ACCOUNT_MONTH2; // 返回弹出选择账单界面
							}
						}
					}
					break;
					
				case 5:
					{
						 state = BILL_NORMAL_STATE;
						step = 6;
					}
					break;
				case 6:
					{
						BSTR pbUrl = NULL;
						pChildWebBrowser->get_LocationURL(&pbUrl);
						if(NULL == pbUrl)
							break;

						wstring url = pbUrl;
						if(url.find(L"/servlet/com.icbc.inbs.person.servlet.ICBCINBSChangeMenuServlet?") == wstring::npos)
							break;

						RecordInfo(L"ICBC", 1800, L"ICBC 5");
					
						step = 7;						
						// 得到起始和截止日期
						std::string strT = pData->select;
						string strTpTime = strT.substr(0, 8);
						strTpTime.insert(4, "-");
						strTpTime.insert(7, "-");
						wstring strBegin = CA2W(strTpTime.c_str());
						
						strTpTime = strT.substr(8, string::npos);
						strTpTime.insert(4, "-");
						strTpTime.insert(7, "-");
						wstring strEnd = CA2W(strTpTime.c_str());

						memset(pData->select, 0, sizeof(pData->select));// 该部分内存是在DLL中申请，所以在这里释放
						// 用户选择要导出的账号
						string strAccountID;

						list<SELECTINFONODE>::const_iterator site = plRecords->m_mapBack.begin();;
						for (;site != plRecords->m_mapBack.end(); site ++)
						{
							if((*site).dwVal == CHECKBOX_SHOW_CHECKED)
							{
								strAccountID = (*site).szNodeInfo;							
								break;
							}
						}
						plRecords->m_mapBack.clear(); // 该部分内存是在DLL中申请，所以在这里释放

						if(strAccountID == "")
						{
							state = BILL_COM_ERROR;
							break;
						}						
						// 卡号
						wstring cardNum = CA2W(strAccountID.c_str());

						CString cstrParam;
						//获到余额时发给工行服务器的相关参数
						cstrParam.Format(L"/servlet/com.icbc.by.datastruct.servlet.AsynGetDataServlet?SessionId=%s&cardNum=%s&acctNum=\
										   &acctType=007&acctCode=&cashSign=0&currType=001&align=undefined&operatorFlag=0&tranflag=0\
										   &tranCode=A00012", g_strDseID.c_str(), cardNum.c_str());
						wstring strParam = cstrParam;
						
						std::map<int, string>  mapSubBalance;
						wstring revinfo;

						if (TransferDataWithServe(g_strDseID, strParam, revinfo) == 0)
						{
							CHECHSTATE;
							RecordInfo(L"ICBC", 1800, L"读取账户余额");

							// 读取余额
							ReadSumBalanceValue(plRecords, revinfo);//从这里面去得到是否是单币种或是双币种

							CHECHSTATE;
							
						}
						else
						{						
							LPBILLRECORD prRecord = new BILLRECORD;
							prRecord->type = RMB;
							prRecord->balance = "F";
							plRecords->BillRecordlist.push_back(prRecord);

							LPBILLRECORD puRecord = new BILLRECORD;
							puRecord->type = USD;
							puRecord->balance = "F";
							plRecords->BillRecordlist.push_back(puRecord);
						}
						

						// 导入各种货币的账单，现在只导了人民币和美元两种
						ImportAllCurrencyTypeBill(strAccountID, strBegin.c_str(), strEnd.c_str(),plRecords, state);
						if (NULL != plRecords)
						{
							strcpy_s(plRecords->aid, 256, "a003");
							strcpy_s(plRecords->tag, 256, strAccountID.c_str());
							plRecords->type = pData->type;
							plRecords->accountid = pData->accountid;
							plRecords->isFinish = true; // 标记导入账单完成
						}
						state = BILL_FINISH_STATE;
						
					}

					break;
				
				default:
					break;	
				}
			}
		}

		if (elem) { elem->Release(); elem = NULL; }
		if (doc3) { doc3->Release(); doc3 = NULL; }
		if (doc2) { doc2->Release(); doc2 = NULL; }
		if (docDisp) { docDisp->Release(); docDisp = NULL; }
	}
	return state;
}

CComVariant CallJScript(IHTMLDocument2 *doc, std::string strFunc,std::vector<std::string>& paramVec)
{
	//Getting IDispatch for Java Script objects
	if(!doc)
		return false;

	// 获得JS脚本
	CComPtr<IDispatch> spScript;
	if(!SUCCEEDED(doc->get_Script(&spScript)))
		return false;
	//
	USES_CONVERSION;
	BSTR bstrMember = ::SysAllocString((LPOLESTR)A2COLE(strFunc.c_str()));
	DISPID dispid = NULL;
	HRESULT hr = spScript->GetIDsOfNames(IID_NULL,&bstrMember,1,
		LOCALE_SYSTEM_DEFAULT,&dispid);
	if(FAILED(hr))
	{
		return false;
	}

	const int arraySize = paramVec.size();
	//Putting parameters
	DISPPARAMS dispparams;
	memset(&dispparams, 0, sizeof dispparams);
	dispparams.cArgs      = arraySize;
	dispparams.rgvarg     = new VARIANT[dispparams.cArgs];
	dispparams.cNamedArgs = 0;


	std::vector<std::string>::iterator ite = paramVec.begin();
	int i = 0;
	for( ;ite != paramVec.end(); ite ++)
	{
		//A2COLE是在栈中分配的空间，如果循环调用，有栈溢出，所以要限制适用调用的次数
		dispparams.rgvarg[i].bstrVal = ::SysAllocString((LPOLESTR)A2COLE((*ite).c_str()));
		dispparams.rgvarg[i].vt = VT_BSTR;
		i ++;
	}
	EXCEPINFO excepInfo;
	memset(&excepInfo, 0, sizeof excepInfo);
	CComVariant vaResult;
	UINT nArgErr = (UINT) -1;      
	// initialize to invalid arg
	// Call JavaScript function
	hr = spScript->Invoke(dispid,IID_NULL,0,DISPATCH_METHOD,&dispparams,&vaResult,&excepInfo,&nArgErr);

	// 释放所有申请的空间
	for(i = 0; i< arraySize;i ++)
	{
		::SysFreeString(dispparams.rgvarg[i].bstrVal);
		dispparams.rgvarg[i].bstrVal = NULL;
	}
	delete[] dispparams.rgvarg;
	::SysFreeString(bstrMember);
	bstrMember = NULL;

	if(FAILED(hr))
	{
		return false;
	}

	return vaResult;
}
