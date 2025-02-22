/*
 * LegacyClonk
 *
 * Copyright (c) RedWolf Design
 * Copyright (c) 2005, Sven2
 * Copyright (c) 2017-2020, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

// Startup screen for non-parameterized engine start: Player selection dialog
// Also contains player creation, editing and crew management

#include "C4GuiEdit.h"
#include "C4GuiListBox.h"
#include <C4Include.h>
#include <C4StartupPlrSelDlg.h>

#include <C4StartupMainDlg.h>
#include <C4Random.h>
#include <C4Game.h>
#include <C4FileSelDlg.h>
#include <C4Log.h>
#include "C4TextEncoding.h"

#include <format>

// font clrs
const uint32_t ClrPlayerItem = 0xff000000;

// C4Utilities

static std::string TimeString(int iSeconds)
{
	int iHours = iSeconds / 3600; iSeconds -= 3600 * iHours;
	int iMinutes = iSeconds / 60; iSeconds -= 60 * iMinutes;
	return std::format("{:02}:{:02}:{:02}", iHours, iMinutes, iSeconds);
}

static std::string DateString(int iTime)
{
	if (!iTime) return "";
	time_t tTime = iTime;
	struct tm *pLocalTime;
	pLocalTime = localtime(&tTime);
	return std::format("{:02}.{:02}.{} {:02}:{:02}",
		pLocalTime->tm_mday,
		pLocalTime->tm_mon + 1,
		pLocalTime->tm_year + 1900,
		pLocalTime->tm_hour,
		pLocalTime->tm_min);
}

// Fixme: This should use the already open group from C4GraphicsResource
static bool GetPortrait(char **ppBytes, size_t *ipSize)
{
	// select random portrait from Graphics.c4g
	C4Group GfxGroup;
	int iCount;
	if (!GfxGroup.Open(Config.AtExePath(C4CFN_Graphics))) return false;
	if ((iCount = GfxGroup.EntryCount("Portrait*.png")) < 1) return false;
	if (!GfxGroup.LoadEntry(std::format("Portrait{}.png", SafeRandom(iCount) + 1).c_str(), ppBytes, ipSize)) return false;
	GfxGroup.Close();
	return true;
}

// C4StartupPlrSelDlg::ListItem

C4StartupPlrSelDlg::ListItem::ListItem(C4StartupPlrSelDlg *pForDlg, C4GUI::ListBox *pForListBox, C4GUI::Element *pInsertBeforeElement, bool fActivated)
	: Control(C4Rect(0, 0, 0, 0)), pCheck(nullptr), pIcon(nullptr), pNameLabel(nullptr), pPlrSelDlg(pForDlg)
{
	CStdFont &rUseFont = C4Startup::Get()->Graphics.BookFont;
	// calc height
	int32_t iHeight = rUseFont.GetLineHeight() + 2 * IconLabelSpacing;
	// create subcomponents
	pCheck = new C4GUI::CheckBox(C4Rect(0, 0, iHeight, iHeight), "", fActivated);
	pCheck->SetOnChecked(new C4GUI::CallbackHandler<C4StartupPlrSelDlg>(pForDlg, &C4StartupPlrSelDlg::OnItemCheckChange));
	pKeyCheck = new C4KeyBinding(C4KeyCodeEx(K_SPACE), "StartupPlrSelTogglePlayerActive", KEYSCOPE_Gui,
		new C4GUI::ControlKeyCB<ListItem>(*this, &ListItem::KeyCheck), C4CustomKey::PRIO_Ctrl);
	pIcon = new C4GUI::Icon(C4Rect(iHeight + IconLabelSpacing, 0, iHeight, iHeight), C4GUI::Ico_Player);
	pNameLabel = new C4GUI::Label("Q", (iHeight + IconLabelSpacing) * 2, IconLabelSpacing, ALeft, ClrPlayerItem, &rUseFont, false, false);
	pNameLabel->SetAutosize(false);
	// calc own bounds - use icon bounds only, because only the height is used when the item is added
	SetBounds(pIcon->GetBounds());
	// add components
	AddElement(pCheck);
	AddElement(pIcon); AddElement(pNameLabel);
	// add to listbox (will get resized horizontally and moved) - zero indent; no tree structure in this dialog
	pForListBox->InsertElement(this, pInsertBeforeElement, 0);
	// update name label width to stretch max listbox width
	C4Rect rcNameLabelBounds = pNameLabel->GetBounds();
	rcNameLabelBounds.Wdt = GetClientRect().Wdt - rcNameLabelBounds.x - IconLabelSpacing;
	pNameLabel->SetBounds(rcNameLabelBounds);
	// context menu
	SetContextHandler(new C4GUI::CBContextHandler<C4StartupPlrSelDlg::ListItem>(this, &C4StartupPlrSelDlg::ListItem::ContextMenu));
}

C4StartupPlrSelDlg::ListItem::~ListItem()
{
	delete pKeyCheck;
}

const char *C4StartupPlrSelDlg::ListItem::GetName() const
{
	// name is stored in label only
	return pNameLabel->GetText();
}

void C4StartupPlrSelDlg::ListItem::SetName(const char *szNewName)
{
	// update name in label
	pNameLabel->SetText(szNewName);
	// tooltip by name, so long names can be read via tooltip
	SetToolTip(szNewName);
}

void C4StartupPlrSelDlg::ListItem::GrabIcon(C4FacetExSurface &rFromFacet)
{
	// take over icon gfx from facet - deletes them from source facet!
	if (rFromFacet.Surface)
	{
		pIcon->GetMFacet().GrabFrom(rFromFacet);
	}
	else
	{
		// reset custom icon
		// following update-call will reset to default icon
		pIcon->GetMFacet().Clear();
	}
}

void C4StartupPlrSelDlg::ListItem::SetIcon(C4GUI::Icons icoNew)
{
	pIcon->SetIcon(icoNew);
}

void C4StartupPlrSelDlg::ListItem::LoadPortrait(C4Group &rGrp, bool fUseDefault)
{
	bool fPortraitLinked = false;
	if (!rGrp.FindEntry(C4CFN_Portrait) || !fctPortraitBase.Load(rGrp, C4CFN_Portrait))
		if (!rGrp.FindEntry(C4CFN_Portrait_Old) || !fctPortraitBase.Load(rGrp, C4CFN_Portrait_Old))
		{
			// no custom portrait: Link to some default if desired
			if (!fUseDefault) return;
			SetDefaultPortrait();
			fPortraitLinked = true;
		}
	if (!fPortraitLinked) CreateColoredPortrait();
}

void C4StartupPlrSelDlg::ListItem::CreateColoredPortrait()
{
	if (fctPortrait.CreateClrByOwner(fctPortraitBase.Surface))
	{
		fctPortrait.Wdt = fctPortraitBase.Wdt;
		fctPortrait.Hgt = fctPortraitBase.Hgt;
	}
}

void C4StartupPlrSelDlg::ListItem::SetDefaultPortrait()
{
	fctPortrait.Set(Game.GraphicsResource.fctPlayerClr);
}

void C4StartupPlrSelDlg::ListItem::GrabPortrait(C4FacetExSurface *pFromFacet)
{
	if (pFromFacet && pFromFacet->Surface)
	{
		fctPortraitBase.GrabFrom(*pFromFacet);
		CreateColoredPortrait();
	}
	else
	{
		SetDefaultPortrait();
	}
}

void C4StartupPlrSelDlg::ListItem::UpdateOwnPos()
{
	// parent for client rect
	typedef C4GUI::Window ParentClass;
	ParentClass::UpdateOwnPos();
	// reposition items
	C4GUI::ComponentAligner caBounds(GetContainedClientRect(), IconLabelSpacing, IconLabelSpacing);
	// nothing to reposition for now...
}

void C4StartupPlrSelDlg::ListItem::SetFilename(const StdStrBuf &sNewFN)
{
	// just set fn - UpdateCore-call will follow later
	Filename.Copy(sNewFN);
}

bool C4StartupPlrSelDlg::ListItem::CheckNameHotkey(const char *c)
{
	// return whether this item can be selected by entering given char:
	// first char of name must match
	// FIXME: Unicode
	if (!pNameLabel) return false;
	const char *szName = pNameLabel->GetText();
	return szName && (toupper(*szName) == toupper(c[0]));
}

// C4StartupPlrSelDlg::PlayerListItem

C4StartupPlrSelDlg::PlayerListItem::PlayerListItem(C4StartupPlrSelDlg *pForDlg, C4GUI::ListBox *pForListBox, C4GUI::Element *pInsertBeforeElement, bool fActivated)
	: ListItem(pForDlg, pForListBox, pInsertBeforeElement, fActivated), fHasCustomIcon(false) {}

void C4StartupPlrSelDlg::PlayerListItem::Load(const StdStrBuf &rsFilename)
{
	int32_t iHeight = GetBounds().Hgt;
	// backup filename
	SetFilename(rsFilename);
	// load player info
	C4Group PlrGroup;
	if (!PlrGroup.Open(rsFilename.getData()))
		throw LoadError(std::format("Error loading player file from {}: Error opening group: {}", rsFilename.getData(), PlrGroup.GetError()));
	if (!Core.Load(PlrGroup))
		throw LoadError(std::format("Error loading player file from {}: Core data invalid or missing (Group: {})!", rsFilename.getData(), PlrGroup.GetError()));
	// load icon
	C4FacetExSurface fctIcon;
	if (PlrGroup.FindEntry(C4CFN_BigIcon) && fctIcon.Load(PlrGroup, C4CFN_BigIcon))
		fHasCustomIcon = true;
	else
	{
		// no custom icon: create default by player color
		fctIcon.Create(iHeight, iHeight);
		Game.GraphicsResource.fctPlayerClr.DrawClr(fctIcon, true, Core.PrefColorDw);
	}
	GrabIcon(fctIcon);
	// load portrait
	LoadPortrait(PlrGroup, true);
	// done loading
	if (!PlrGroup.Close())
		throw LoadError(std::format("Error loading player file from {}: Error closing group: {}", rsFilename.getData(), PlrGroup.GetError()));
	// default name
	if (!*Core.PrefName) SCopy(GetFilenameOnly(rsFilename.getData()), Core.PrefName, sizeof(Core.PrefName) - 1);
	SetName(Core.PrefName);
}

C4GUI::ContextMenu *C4StartupPlrSelDlg::PlayerListItem::ContextMenu()
{
	// menu operations work on selected item only
	pPlrSelDlg->SetSelection(this);
	// context menu operations
	C4GUI::ContextMenu *pCtx = new C4GUI::ContextMenu();
	pCtx->AddItem(LoadResStr(C4ResStrTableKey::IDS_BTN_PROPERTIES), LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERPROPERTIES), C4GUI::Ico_None, new C4GUI::CBMenuHandler<C4StartupPlrSelDlg>(pPlrSelDlg, &C4StartupPlrSelDlg::OnPropertyCtx));
	pCtx->AddItem(LoadResStr(C4ResStrTableKey::IDS_BTN_DELETE), LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERDELETE), C4GUI::Ico_None, new C4GUI::CBMenuHandler<C4StartupPlrSelDlg>(pPlrSelDlg, &C4StartupPlrSelDlg::OnDelCtx));
	return pCtx;
}

void C4StartupPlrSelDlg::PlayerListItem::GrabCustomIcon(C4FacetExSurface &fctGrabFrom)
{
	// set flag
	fHasCustomIcon = !!fctGrabFrom.Surface;
	// base class grab
	GrabIcon(fctGrabFrom);
}

void C4StartupPlrSelDlg::PlayerListItem::UpdateCore(C4PlayerInfoCore &NewCore)
{
	C4Group PlrGroup;
	if (!PlrGroup.Open(GetFilename().getData())
		|| !NewCore.Save(PlrGroup)
		|| !PlrGroup.Close())
	{
		GetScreen()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_FAIL_MODIFY), "", C4GUI::Ico_Error);
		return;
	}
	Core = NewCore;
	SetName(Core.PrefName);
	// re-set non-custom icons
	if (!fHasCustomIcon)
	{
		fHasCustomIcon = false;
		int32_t iHeight = GetBounds().Hgt;
		C4FacetExSurface fctIcon; fctIcon.Create(iHeight, iHeight);
		Game.GraphicsResource.fctPlayerClr.DrawClr(fctIcon, true, Core.PrefColorDw);
		GrabIcon(fctIcon);
	}
	// update in selection
	C4StartupPlrSelDlg *pDlg = static_cast<C4StartupPlrSelDlg *>(GetDlg());
	if (pDlg && pDlg->GetSelection() == this) pDlg->UpdateSelection();
}

void C4StartupPlrSelDlg::PlayerListItem::SetSelectionInfo(C4GUI::TextWindow *pSelectionInfo)
{
	// write info text for player
	pSelectionInfo->ClearText(false);
	pSelectionInfo->AddTextLine(Core.PrefName, &C4Startup::Get()->Graphics.BookFontCapt, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(LoadResStr(C4ResStrTableKey::IDS_DESC_PLAYER, static_cast<int>(Core.Score), static_cast<int>(Core.Rounds), static_cast<int>(Core.RoundsWon), static_cast<int>(Core.RoundsLost), TimeString(Core.TotalPlayingTime), Core.Comment).c_str(), &C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	if (Core.LastRound.Title[0])
		pSelectionInfo->AddTextLine(LoadResStr(C4ResStrTableKey::IDS_DESC_LASTGAME, Core.LastRound.Title.getData(), DateString(Core.LastRound.Date), TimeString(Core.LastRound.Duration), static_cast<int>(Core.LastRound.FinalScore)).c_str(), &C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->UpdateHeight();
}

std::string C4StartupPlrSelDlg::PlayerListItem::GetDelWarning()
{
	std::string warning{LoadResStr(C4ResStrTableKey::IDS_MSG_DELETEPLR, Core.PrefName)};
	int32_t iPlrTime = Core.TotalPlayingTime;
	if (iPlrTime > 60 * 60 * 10)
		warning += LoadResStr(C4ResStrTableKey::IDS_MSG_DELETEPLR_PLAYTIME,
			TimeString(iPlrTime));
	return warning;
}

bool C4StartupPlrSelDlg::PlayerListItem::MoveFilename(const char *szToFilename)
{
	// anything to do?
	if (ItemIdentical(GetFilename().getData(), szToFilename)) return true;
	// do it
	if (!MoveItem(GetFilename().getData(), szToFilename)) return false;
	// reflect change in class
	SetFilename(StdStrBuf::MakeRef(szToFilename));
	return true;
}

// C4StartupPlrSelDlg::CrewListItem

C4StartupPlrSelDlg::CrewListItem::CrewListItem(C4StartupPlrSelDlg *pForDlg, C4GUI::ListBox *pForListBox, uint32_t dwPlrClr)
	: ListItem(pForDlg, pForListBox, nullptr, false), fLoaded(false), dwPlrClr(dwPlrClr), pParentGrp(nullptr)
{
	SetIcon(C4GUI::Ico_Wait);
}

void C4StartupPlrSelDlg::CrewListItem::UpdateClonkEnabled()
{
	if (!fLoaded) return;
	Core.Participation = pCheck->GetChecked();
	// immediate save of changes
	RewriteCore();
}

void C4StartupPlrSelDlg::CrewListItem::Load(C4Group &rGrp, const StdStrBuf &rsFilename)
{
	// backup filename (doesn't include path)
	SetFilename(rsFilename);
	// load core
	C4Group CrewGroup;
	if (!CrewGroup.OpenAsChild(&rGrp, rsFilename.getData()))
		throw LoadError(std::format("Error loading crew file from {} in {}: Error opening group: {}",
			rsFilename.getData(), rGrp.GetFullName().getData(), CrewGroup.GetError()));
	if (!Core.Load(CrewGroup))
		throw LoadError(std::format("Error loading crew file from {}: Core data invalid or missing (Group: {})!",
			CrewGroup.GetFullName().getData(), CrewGroup.GetError()));
	ListItem::SetName(Core.Name);
	pCheck->SetChecked(!!Core.Participation);
	// load rank as icon
	C4FacetExSurface fctIcon;
	if (fctIcon.Load(CrewGroup, C4CFN_ClonkRank, C4FCT_Full, C4FCT_Full, false, true))
	{
		GrabIcon(fctIcon);
	}
	else
	{
		// no custom icon: create default by rank system
		if (C4RankSystem::DrawRankSymbol(&fctIcon, Core.Rank, &Game.GraphicsResource.fctRank, Game.GraphicsResource.iNumRanks, true))
			GrabIcon(fctIcon);
	}
	// load portrait; empty by default
	LoadPortrait(CrewGroup, false);
	// backup group loaded from - assumes it stays valid!
	pParentGrp = &rGrp;
	// load success!
	fLoaded = true;
}

C4GUI::ContextMenu *C4StartupPlrSelDlg::CrewListItem::ContextMenu()
{
	// menu operations work on selected item only
	pPlrSelDlg->SetSelection(this);
	// context menu operations
	C4GUI::ContextMenu *pCtx = new C4GUI::ContextMenu();
	pCtx->AddItem(LoadResStr(C4ResStrTableKey::IDS_BTN_RENAME), LoadResStr(C4ResStrTableKey::IDS_DESC_CREWRENAME), C4GUI::Ico_None, new C4GUI::CBMenuHandler<C4StartupPlrSelDlg>(pPlrSelDlg, &C4StartupPlrSelDlg::OnPropertyCtx));
	pCtx->AddItem(LoadResStr(C4ResStrTableKey::IDS_BTN_DELETE), LoadResStr(C4ResStrTableKey::IDS_MSG_DELETECLONK_DESC), C4GUI::Ico_None, new C4GUI::CBMenuHandler<C4StartupPlrSelDlg>(pPlrSelDlg, &C4StartupPlrSelDlg::OnDelCtx));
	pCtx->AddItem(LoadResStr(C4ResStrTableKey::IDS_MSG_SETDEATHMESSAGE), LoadResStr(C4ResStrTableKey::IDS_MSG_SETTHEMESSAGETHATAPPEARWH), C4GUI::Ico_None, new C4GUI::CBMenuHandler<C4StartupPlrSelDlg::CrewListItem>(this, &C4StartupPlrSelDlg::CrewListItem::OnDeathMessageCtx));
	return pCtx;
}

void C4StartupPlrSelDlg::CrewListItem::OnDeathMessageCtx(C4GUI::Element *el)
{
	// Death message dialog
	C4GUI::InputDialog *pDlg;
	GetScreen()->ShowRemoveDlg(pDlg = new C4GUI::InputDialog(LoadResStr(C4ResStrTableKey::IDS_MSG_ENTERNEWDEATHMESSAGE), LoadResStr(C4ResStrTableKey::IDS_MSG_SETDEATHMESSAGE), C4GUI::Ico_Ex_Comment, new C4GUI::InputCallback<C4StartupPlrSelDlg::CrewListItem>(this, &C4StartupPlrSelDlg::CrewListItem::OnDeathMessageSet), false));
	pDlg->SetMaxText(C4MaxDeathMsg);
	pDlg->SetInputText(Core.DeathMessage);
}

void C4StartupPlrSelDlg::CrewListItem::OnDeathMessageSet(const StdStrBuf &rsNewMessage)
{
	// copy msg
	if (!rsNewMessage) *Core.DeathMessage = '\0'; else SCopy(rsNewMessage.getData(), Core.DeathMessage, C4MaxDeathMsg);
	// save
	RewriteCore();
	// acoustic feedback
	C4GUI::GUISound("Connect");
}

void C4StartupPlrSelDlg::CrewListItem::RewriteCore()
{
	if (!fLoaded) return;
	C4Group CrewGroup;
	if (!CrewGroup.OpenAsChild(pParentGrp, GetFilename().getData())
		|| !Core.Save(CrewGroup, nullptr)
		|| !CrewGroup.Close() || !pParentGrp->Save(true))
	{
		GetScreen()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_FAIL_MODIFY), "", C4GUI::Ico_Error);
		return;
	}
}

bool C4StartupPlrSelDlg::CrewListItem::SetName(const char *szNewName)
{
	if (!fLoaded) return false;
	// validate name
	if (!szNewName || !*szNewName) return false;
	if (SEqual(szNewName, Core.Name)) return true;
	// generate filename from new name
	char fn[_MAX_PATH + 1];
	SCopy(szNewName, fn, _MAX_PATH);
	MakeFilenameFromTitle(fn);
	if (!*fn) return false;
	SAppend(".c4i", fn, _MAX_PATH);
	// check if a rename is due
	if (!ItemIdentical(fn, GetFilename().getData()))
	{
		// check for duplicate filename
		if (pParentGrp->FindEntry(fn))
		{
			const std::string msg{LoadResStr(C4ResStrTableKey::IDS_ERR_CLONKCOLLISION, fn)};
			Game.pGUI->ShowMessageModal(msg.c_str(), LoadResStr(C4ResStrTableKey::IDS_FAIL_RENAME), C4GUI::MessageDialog::btnOK, C4GUI::Ico_Error);
			return false;
		}
		// OK; then rename
		if (!pParentGrp->Rename(GetFilename().getData(), fn) || !pParentGrp->Save(true))
		{
			const std::string msg{LoadResStr(C4ResStrTableKey::IDS_ERR_RENAMEFILE, GetFilename().getData(), fn)};
			Game.pGUI->ShowMessageModal(msg.c_str(), LoadResStr(C4ResStrTableKey::IDS_FAIL_RENAME), C4GUI::MessageDialog::btnOK, C4GUI::Ico_Error);
			return false;
		}
		const char *szConstFn = fn;
		SetFilename(StdStrBuf::MakeRef(szConstFn));
	}
	// update clonk name and core
	ListItem::SetName(szNewName);
	SCopy(szNewName, Core.Name, C4MaxName);
	RewriteCore();
	return true;
}

std::string C4StartupPlrSelDlg::CrewListItem::GetPhysicalTextLine(int32_t iPhysValue, const C4ResStrTableKeyFormat<> idsName)
{
	const int32_t iMaxBars = 10;
	std::string result{LoadResStr(idsName)};
	result += ' ';
	result.append(iMaxBars * iPhysValue / C4MaxPhysical, '\xb7' /*·*/);
	return result;
}

void C4StartupPlrSelDlg::CrewListItem::SetSelectionInfo(C4GUI::TextWindow *pSelectionInfo)
{
	// write info text for player
	pSelectionInfo->ClearText(false);
	pSelectionInfo->AddTextLine(std::format("{} {}", Core.sRankName.getData(), +Core.Name).c_str(), &C4Startup::Get()->Graphics.BookFontCapt, ClrPlayerItem, false, false);
	std::string promo;
	int32_t iNextRankExp; StdStrBuf sNextRankName;
	if (Core.GetNextRankInfo(Game.Rank, &iNextRankExp, &sNextRankName))
		promo = LoadResStr(C4ResStrTableKey::IDS_DESC_PROMO, sNextRankName.getData(), static_cast<int>(iNextRankExp));
	else
		promo = LoadResStr(C4ResStrTableKey::IDS_DESC_NOPROMO);
	pSelectionInfo->AddTextLine(LoadResStr(C4ResStrTableKey::IDS_DESC_OBJECT,
		Core.TypeName, Core.Experience, Core.Rounds, Core.DeathCount,
		promo.c_str(), TimeString(Core.TotalPlayingTime), DateString(Core.Birthday)).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Energy, C4ResStrTableKey::IDS_DESC_ENERGY).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Breath, C4ResStrTableKey::IDS_DESC_BREATH).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Walk, C4ResStrTableKey::IDS_DESC_WALK).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Jump, C4ResStrTableKey::IDS_DESC_JUMP).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	if (Core.Physical.CanScale) pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Scale, C4ResStrTableKey::IDS_DESC_SCALE).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	if (Core.Physical.CanHangle) pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Hangle, C4ResStrTableKey::IDS_DESC_HANGLE).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Dig, C4ResStrTableKey::IDS_DESC_DIG).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Swim, C4ResStrTableKey::IDS_DESC_SWIM).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Throw, C4ResStrTableKey::IDS_DESC_THROW).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Push, C4ResStrTableKey::IDS_DESC_PUSH).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Fight, C4ResStrTableKey::IDS_DESC_FIGHT).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	if (Core.Physical.Magic) pSelectionInfo->AddTextLine(GetPhysicalTextLine(Core.Physical.Magic, C4ResStrTableKey::IDS_DESC_MAGIC).c_str(),
		&C4Startup::Get()->Graphics.BookFont, ClrPlayerItem, false, false);
	pSelectionInfo->UpdateHeight();
}

std::string C4StartupPlrSelDlg::CrewListItem::GetDelWarning()
{
	std::string warning{LoadResStr(C4ResStrTableKey::IDS_MSG_DELETECLONK,
		Core.sRankName.getData(), Core.Name)};
	int32_t iPlrTime = Core.TotalPlayingTime;
	if (iPlrTime > 60 * 60 * 10)
		warning += LoadResStr(C4ResStrTableKey::IDS_MSG_DELETECLONK_PLAYTIME, TimeString(iPlrTime));
	return warning;
}

void C4StartupPlrSelDlg::CrewListItem::CrewRename()
{
	if (pPlrSelDlg->pRenameEdit) return;
	// rename this entry
	pPlrSelDlg->pRenameEdit = new C4GUI::CallbackRenameEdit<C4StartupPlrSelDlg::CrewListItem, RenameParams>(pNameLabel, this, RenameParams(), &C4StartupPlrSelDlg::CrewListItem::DoRenaming, &C4StartupPlrSelDlg::CrewListItem::AbortRenaming);
}

void C4StartupPlrSelDlg::CrewListItem::AbortRenaming(RenameParams par)
{
	// no renaming
	pPlrSelDlg->pRenameEdit = nullptr;
}

C4GUI::RenameResult C4StartupPlrSelDlg::CrewListItem::DoRenaming(RenameParams par, const char *szNewName)
{
	// accept if name can be set; will fail if name is invalid or already given to another Crew member
	if (!SetName(szNewName)) return C4GUI::RR_Invalid;
	pPlrSelDlg->pRenameEdit = nullptr;
	// update in selection
	C4StartupPlrSelDlg *pDlg = static_cast<C4StartupPlrSelDlg *>(GetDlg());
	if (pDlg && pDlg->GetSelection() == this) pDlg->UpdateSelection();
	return C4GUI::RR_Accepted;
}

// C4StartupPlrSelDlg

C4StartupPlrSelDlg::C4StartupPlrSelDlg() : C4StartupDlg("W"), eMode(PSDM_Player), pRenameEdit(nullptr)
{
	UpdateSize(); // for clientrect

	// screen calculations
	int iButtonHeight = C4GUI_ButtonHgt;
	int iButtonXSpacing = (GetClientRect().Wdt > 700) ? GetClientRect().Wdt / 58 : 2;
	int iButtonCount = 6;
	C4GUI::ComponentAligner caMain(GetClientRect(), 0, 0, true);
	C4GUI::ComponentAligner caButtonArea(caMain.GetFromBottom((std::max)(caMain.GetHeight() / 15, iButtonHeight)), 0, 0);
	rcBottomButtons = caButtonArea.GetCentered(caMain.GetWidth(), iButtonHeight);
	iBottomButtonWidth = (caButtonArea.GetWidth() - iButtonXSpacing * (iButtonCount - 1)) / iButtonCount;
	C4Rect rcMain = caMain.GetAll();
	C4Rect rcPlrList = C4Rect(rcMain.Wdt / 10, rcMain.Hgt * 10 / 36, rcMain.Wdt * 25 / 81, rcMain.Hgt * 2 / 3);
	C4Rect rcInfoWindow = C4Rect(rcMain.Wdt * 371 / 768, rcMain.Hgt * 197 / 451, rcMain.Wdt * 121 / 384, rcMain.Hgt * 242 / 451);
	int iPictureWidth = (std::min)(rcMain.Wdt * 121 / 384, 200);
	int iPictureHeight = iPictureWidth * 3 / 4;
	C4Rect rcPictureArea = C4Rect(rcMain.Wdt * 613 / 768 - iPictureWidth, rcMain.Hgt * 197 / 451 - iPictureHeight, iPictureWidth, iPictureHeight);

	AddElement(pPlrListBox = new C4GUI::ListBox(rcPlrList));
	pPlrListBox->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERFILES));
	pPlrListBox->SetDecoration(false, &C4Startup::Get()->Graphics.sfctBookScroll, true);
	pPlrListBox->UpdateElementPositions();
	pPlrListBox->SetSelectionChangeCallbackFn(new C4GUI::CallbackHandler<C4StartupPlrSelDlg>(this, &C4StartupPlrSelDlg::OnSelChange));
	pPlrListBox->SetSelectionDblClickFn(new C4GUI::CallbackHandler<C4StartupPlrSelDlg>(this, &C4StartupPlrSelDlg::OnSelDblClick));
	AddElement(pSelectionInfo = new C4GUI::TextWindow(rcInfoWindow));
	pSelectionInfo->SetDecoration(false, false, &C4Startup::Get()->Graphics.sfctBookScroll, true);
	pSelectionInfo->UpdateHeight();
	AddElement(pPortraitPict = new C4GUI::Picture(rcPictureArea, true));

	// bottom line buttons - positioning done in UpdateBottomButtons by UpdatePlayerList
	C4Rect rcDefault(0, 0, 10, 10);
	AddElement(btnBack = new C4GUI::CallbackButton<C4StartupPlrSelDlg>(LoadResStr(C4ResStrTableKey::IDS_BTN_BACK), rcDefault, &C4StartupPlrSelDlg::OnBackBtn));
	AddElement(btnNew = new C4GUI::CallbackButton<C4StartupPlrSelDlg>(LoadResStr(C4ResStrTableKey::IDS_BTN_NEW), rcDefault, &C4StartupPlrSelDlg::OnNewBtn));
	btnNew->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_NEWPLAYER));
	AddElement(btnActivatePlr = new C4GUI::CallbackButton<C4StartupPlrSelDlg>(nullptr, rcDefault, &C4StartupPlrSelDlg::OnActivateBtn));
	AddElement(btnDelete = new C4GUI::CallbackButton<C4StartupPlrSelDlg>(LoadResStr(C4ResStrTableKey::IDS_BTN_DELETE), rcDefault, &C4StartupPlrSelDlg::OnDelBtn));
	AddElement(btnProperties = new C4GUI::CallbackButton<C4StartupPlrSelDlg>(nullptr, rcDefault, &C4StartupPlrSelDlg::OnPropertyBtn));
	AddElement(btnCrew = new C4GUI::CallbackButton<C4StartupPlrSelDlg>(LoadResStr(C4ResStrTableKey::IDS_SELECT_CREW), rcDefault, &C4StartupPlrSelDlg::OnCrewBtn));
	btnCrew->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERCREW));

	// refill listboxes
	UpdatePlayerList();
	// Just safety incase listbox was empty, in which case no selection change callback will have been done:
	// Update current listbox selection to that of no selected item
	if (!pPlrListBox->GetFirst()) UpdateSelection();

	// initial focus on player list
	SetFocus(pPlrListBox, false);

	// key bindings
	C4CustomKey::CodeList keys;
	keys.push_back(C4KeyCodeEx(K_BACK));
	keys.push_back(C4KeyCodeEx(K_LEFT));
	keys.push_back(C4KeyCodeEx(K_ESCAPE));
	if (Config.Controls.GamepadGuiControl)
	{
		keys.push_back(C4KeyCodeEx(KEY_Gamepad(0, KEY_JOY_AnyHighButton)));
	}
	pKeyBack = new C4KeyBinding(keys, "StartupPlrSelBack", KEYSCOPE_Gui,
		new C4GUI::DlgKeyCB<C4StartupPlrSelDlg>(*this, &C4StartupPlrSelDlg::KeyBack), C4CustomKey::PRIO_CtrlOverride);
	pKeyProperties = new C4KeyBinding(C4KeyCodeEx(K_F2), "StartupPlrSelProp", KEYSCOPE_Gui,
		new C4GUI::DlgKeyCB<C4StartupPlrSelDlg>(*this, &C4StartupPlrSelDlg::KeyProperties), C4CustomKey::PRIO_CtrlOverride);
	pKeyCrew = new C4KeyBinding(C4KeyCodeEx(K_RIGHT), "StartupPlrSelCrew", KEYSCOPE_Gui,
		new C4GUI::ControlKeyDlgCB<C4StartupPlrSelDlg>(pPlrListBox, *this, &C4StartupPlrSelDlg::KeyCrew), C4CustomKey::PRIO_CtrlOverride);
	pKeyDelete = new C4KeyBinding(C4KeyCodeEx(K_DELETE), "StartupPlrSelDelete", KEYSCOPE_Gui,
		new C4GUI::DlgKeyCB<C4StartupPlrSelDlg>(*this, &C4StartupPlrSelDlg::KeyDelete), C4CustomKey::PRIO_CtrlOverride);
	pKeyNew = new C4KeyBinding(C4KeyCodeEx(K_INSERT), "StartupPlrSelNew", KEYSCOPE_Gui,
		new C4GUI::DlgKeyCB<C4StartupPlrSelDlg>(*this, &C4StartupPlrSelDlg::KeyNew), C4CustomKey::PRIO_CtrlOverride);
}

C4StartupPlrSelDlg::~C4StartupPlrSelDlg()
{
	delete pKeyDelete;
	delete pKeyCrew;
	delete pKeyProperties;
	delete pKeyBack;
	delete pKeyNew;
}

void C4StartupPlrSelDlg::AbortRenaming()
{
	if (pRenameEdit) pRenameEdit->Abort();
}

void C4StartupPlrSelDlg::DrawElement(C4FacetEx &cgo)
{
	// draw background
	DrawBackground(cgo, C4Startup::Get()->Graphics.fctPlrSelBG);
}

void C4StartupPlrSelDlg::UpdateBottomButtons()
{
	// bottom line buttons depend on list mode
	C4GUI::ComponentAligner caBottomButtons(rcBottomButtons, 0, 0);
	switch (eMode)
	{
	case PSDM_Player:
	{
		// update some buttons for player mode
		btnProperties->SetText(LoadResStr(C4ResStrTableKey::IDS_BTN_PROPERTIES));
		btnProperties->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERPROPERTIES));
		btnNew->SetVisibility(true);
		btnCrew->SetVisibility(true);
		btnDelete->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERDELETE));
		btnBack->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_BACKMAIN));
		btnBack->SetBounds(caBottomButtons.GetGridCell(0, 6, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnNew->SetBounds(caBottomButtons.GetGridCell(1, 6, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnActivatePlr->SetBounds(caBottomButtons.GetGridCell(2, 6, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnDelete->SetBounds(caBottomButtons.GetGridCell(3, 6, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnProperties->SetBounds(caBottomButtons.GetGridCell(4, 6, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnCrew->SetBounds(caBottomButtons.GetGridCell(5, 6, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
	}
	break;

	case PSDM_Crew:
	{
		// update some buttons for player mode
		btnProperties->SetText(LoadResStr(C4ResStrTableKey::IDS_BTN_RENAME));
		btnProperties->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DESC_CREWRENAME));
		btnNew->SetVisibility(false);
		btnCrew->SetVisibility(false);
		btnDelete->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_MSG_DELETECLONK_DESC));
		btnBack->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_MSG_BACKTOPLAYERDLG));
		btnBack->SetBounds(caBottomButtons.GetGridCell(0, 4, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnActivatePlr->SetBounds(caBottomButtons.GetGridCell(1, 4, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnDelete->SetBounds(caBottomButtons.GetGridCell(2, 4, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
		btnProperties->SetBounds(caBottomButtons.GetGridCell(3, 4, 0, 1, iBottomButtonWidth, C4GUI_ButtonHgt, true));
	}
	break;
	};
}

void C4StartupPlrSelDlg::UpdatePlayerList()
{
	// something has changed!
	AbortRenaming();
	// refill pPlrListBox with players in player folder or crew
	// clear old items
	C4GUI::Element *pEl;
	while (pEl = pPlrListBox->GetFirst()) delete pEl;
	// update command buttons
	UpdateBottomButtons();
	// create new
	switch (eMode)
	{
	case PSDM_Player:
	{
		SetTitle(LoadResStrNoAmp(C4ResStrTableKey::IDS_DLG_PLAYERSELECTION).c_str());
		// player mode: insert all players
		const char *szFn;
		const std::string searchPath{std::format("{}{}", +Config.General.ExePath, +Config.General.PlayerPath)};
		PlayerListItem *pFirstActivatedPlrItem = nullptr, *pFirstDeactivatedPlrItem = nullptr, *pPlrItem = nullptr;
		for (DirectoryIterator i(searchPath.c_str()); szFn = *i; i++)
		{
			szFn = Config.AtExeRelativePath(szFn);
			if (*GetFilename(szFn) == '.') continue; // ignore ".", ".." and private files (".*")
			if (!WildcardMatch(C4CFN_PlayerFiles, GetFilename(szFn))) continue;
			bool fIsParticipating = !!SIsModule(Config.General.Participants, szFn, nullptr, false);
			pPlrItem = new PlayerListItem(this, pPlrListBox, nullptr, fIsParticipating);
			try
			{
				pPlrItem->Load(StdStrBuf::MakeRef(szFn));
			}
			catch (ListItem::LoadError &e)
			{
				// invalid player: ignore but log error message
				DebugLog(e.what());
				delete pPlrItem;
				continue;
			}
			if (fIsParticipating)
			{
				if (!pFirstActivatedPlrItem) pFirstActivatedPlrItem = pPlrItem;
			}
			else if (!pFirstDeactivatedPlrItem) pFirstDeactivatedPlrItem = pPlrItem;
		}
		// select first element; prefer activated player
		if (!(pPlrItem = pFirstActivatedPlrItem))
			pPlrItem = pFirstDeactivatedPlrItem;
		if (pPlrItem)
			pPlrListBox->SelectEntry(pPlrItem, false);
		// re-validate Game.PlayerFilename
		UpdateActivatedPlayers();
		break;
	}

	case PSDM_Crew:
	{
		SetTitle(std::format("{} {}", LoadResStrNoAmp(C4ResStrTableKey::IDS_CTL_CREW).c_str(), +CurrPlayer.Core.PrefName).c_str());
		// crew mode: Insert complete crew of player (2do: sort)
		bool fSucc; char szFn[_MAX_PATH + 1];
		for (fSucc = CurrPlayer.Grp.FindEntry(C4CFN_ObjectInfoFiles, szFn); fSucc; fSucc = CurrPlayer.Grp.FindNextEntry(C4CFN_ObjectInfoFiles, szFn, nullptr, nullptr, true))
		{
			CrewListItem *pCrewItem = new CrewListItem(this, pPlrListBox, CurrPlayer.Core.PrefColorDw);
			try
			{
				pCrewItem->Load(CurrPlayer.Grp, StdStrBuf(szFn));
			}
			catch (ListItem::LoadError &e)
			{
				// invalid player: ignore but log error message
				DebugLog(e.what());
				delete pCrewItem;
				continue;
			}
		}
		// resort crew by type and experience
		ResortCrew();
		pPlrListBox->SelectFirstEntry(false);
		break;
	}
	}
}

C4StartupPlrSelDlg::ListItem *C4StartupPlrSelDlg::GetSelection()
{
	// get selected item: There's only instances of PlrListItem in this list
	return static_cast<ListItem *>(pPlrListBox->GetSelectedItem());
}

void C4StartupPlrSelDlg::SetSelection(ListItem *pNewItem)
{
	// update selection in listbox
	pPlrListBox->SelectEntry(pNewItem, false);
}

void C4StartupPlrSelDlg::UpdateSelection()
{
	// something has changed!
	AbortRenaming();
	// get currently selected player
	ListItem *pSel = GetSelection();
	// button text 'activate' if current player is deactivated; 'deactivate' otherwise
	if (pSel && pSel->IsActivated())
	{
		btnActivatePlr->SetText(LoadResStr(C4ResStrTableKey::IDS_BTN_DEACTIVATE));
		btnActivatePlr->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_MSG_NOPARTICIPATE_DESC, pSel->GetName()).c_str());
	}
	else
	{
		btnActivatePlr->SetText(LoadResStr(C4ResStrTableKey::IDS_BTN_ACTIVATE));
		btnActivatePlr->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_MSG_PARTICIPATE_DESC, pSel ? pSel->GetName() : "").c_str());
	}
	// no item selected?
	if (!pSel)
	{
		pSelectionInfo->ClearText(true);
		pPortraitPict->GetMFacet().Clear();
		// 2do: disable buttons
		return;
	}
	// info text for selection
	pSel->SetSelectionInfo(pSelectionInfo);
	// portrait for selection
	pPortraitPict->SetFacet(pSel->GetPortrait());
	pPortraitPict->SetDrawColor(pSel->GetColorDw());
}

void C4StartupPlrSelDlg::OnItemCheckChange(C4GUI::Element *pCheckBox)
{
	switch (eMode)
	{
	case PSDM_Player:
		// update Config.General.Participants
		UpdateActivatedPlayers();
		break;
	case PSDM_Crew:
		// update affected crew item
		if (pCheckBox) static_cast<CrewListItem *>(pCheckBox->GetParent())->UpdateClonkEnabled();
		break;
	}
	// update player selection text
	UpdateSelection();
}

void C4StartupPlrSelDlg::UpdateActivatedPlayers()
{
	assert(eMode == PSDM_Player);
	// refill Config.General.Participants-list
	*Config.General.Participants = '\0';
	for (ListItem *pPlrItem = static_cast<ListItem *>(pPlrListBox->GetFirst()); pPlrItem; pPlrItem = pPlrItem->GetNext())
		if (pPlrItem->IsActivated())
		{
			const char *szAddFilename = pPlrItem->GetFilename().getData();
			if (SLen(Config.General.Participants) + 1 + SLen(szAddFilename) < sizeof(Config.General.Participants))
				SAddModule(Config.General.Participants, szAddFilename);
			else
			{
				pPlrItem->SetActivated(false);
				GetScreen()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_ERR_PLAYERSTOOLONG, pPlrItem->GetName()).c_str(), LoadResStr(C4ResStrTableKey::IDS_ERR_TITLE), C4GUI::Ico_Error);
			}
		}
}

void C4StartupPlrSelDlg::OnActivateBtn(C4GUI::Control *btn)
{
	// toggle activation state of current item
	// get currently selected player
	ListItem *pSel = GetSelection();
	if (!pSel) return;
	pSel->SetActivated(!pSel->IsActivated());
	// update stuff
	OnItemCheckChange(nullptr);
}

void C4StartupPlrSelDlg::DoBack()
{
	switch (eMode)
	{
	case PSDM_Player:
	{
		// back 2 main
		C4Startup::Get()->SwitchDialog(C4Startup::SDID_Back);
		break;
	}

	case PSDM_Crew:
		// back 2 player list
		SetPlayerMode();
		break;
	}
}

void C4StartupPlrSelDlg::OnNewBtn(C4GUI::Control *btn)
{
	if (eMode != PSDM_Player) return;
	C4GUI::Dialog *pDlg;
	GetScreen()->ShowRemoveDlg(pDlg = new C4StartupPlrPropertiesDlg(nullptr, this));
	pDlg->SetPos(std::min<int32_t>(GetBounds().Wdt / 10, GetBounds().Wdt - pDlg->GetBounds().Wdt), std::min<int32_t>(GetBounds().Hgt / 4, GetBounds().Hgt - pDlg->GetBounds().Hgt));
}

bool C4StartupPlrSelDlg::CheckPlayerName(const StdStrBuf &Playername, std::string &filename, const StdStrBuf *pPrevFilename, bool fWarnEmpty)
{
	// must not be empty
	if (!Playername.getLength())
	{
		if (fWarnEmpty) C4GUI::Screen::GetScreenS()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_ERR_PLRNAME_EMPTY), "", C4GUI::Ico_Error);
		return false;
	}
	// generate valid filename
	filename = TextEncodingConverter.ClonkToSystem(Playername.getData());
	// Slashes in Filenames are no good
	SReplaceChar(filename.data(), '\\', '_');
	SReplaceChar(filename.data(), '/', '_');
	SReplaceChar(filename.data(), ':', '_');
	SReplaceChar(filename.data(), '*', '_');
	SReplaceChar(filename.data(), '?', '_');
	SReplaceChar(filename.data(), '"', '_');
	SReplaceChar(filename.data(), '<', '_');
	SReplaceChar(filename.data(), '>', '_');
	SReplaceChar(filename.data(), '|', '_');
	if (filename[0] == '.') filename[0] = '_';
	filename.append(".c4p");
	std::string path{Config.General.PlayerPath}; // start at local path
	path.append(filename);
	// validity check: Must not exist yet if renamed
	if (!pPrevFilename || !ItemIdentical(path.c_str(), pPrevFilename->getData())) if (ItemExists(path.c_str()))
	{
		C4GUI::Screen::GetScreenS()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_ERR_PLRNAME_TAKEN,
			Playername.getData()).c_str(), "", C4GUI::Ico_Error);
		return false;
	}
	filename = std::move(path);
	return true;
}

void C4StartupPlrSelDlg::OnCrewBtn(C4GUI::Control *btn)
{
	// display crew for activated player
	if (eMode != PSDM_Player) return;
	PlayerListItem *pSel = static_cast<PlayerListItem *>(GetSelection());
	if (!pSel) return;
	SetCrewMode(pSel);
}

void C4StartupPlrSelDlg::SetPlayerMode()
{
	// change view to listing players
	C4GUI::GUISound("DoorClose");
	StdStrBuf fullName{CurrPlayer.Grp.GetFullName()};
	std::string lastPlrFilename{fullName.getData(), fullName.getLength()};
	CurrPlayer.Grp.Close();
	eMode = PSDM_Player;
	UpdatePlayerList();
	SelectItem(lastPlrFilename, false);
	UpdateSelection();
}

void C4StartupPlrSelDlg::SetCrewMode(PlayerListItem *pSel)
{
	// change view to listing crew of a player
	CurrPlayer.Core = pSel->GetCore();
	if (!CurrPlayer.Grp.Open(pSel->GetFilename().getData())) return;
	if (!CurrPlayer.Grp.FindEntry(C4CFN_ObjectInfoFiles))
	{
		const std::string crew{std::format("{} {}", LoadResStrNoAmp(C4ResStrTableKey::IDS_CTL_CREW), +CurrPlayer.Core.PrefName)};
		// player has no crew!
		GetScreen()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_ERR_PLRNOCREW,
			CurrPlayer.Core.PrefName).c_str(),
			crew.c_str(), C4GUI::Ico_Player);
		return;
	}
	C4GUI::GUISound("DoorOpen");
	eMode = PSDM_Crew;
	UpdatePlayerList();
	UpdateSelection();
}

void C4StartupPlrSelDlg::OnDelBtn(C4GUI::Control *btn)
{
	// something has changed!
	AbortRenaming();
	// delete selected player
	ListItem *pSel = GetSelection();
	if (!pSel) return;
	const std::string warning{pSel->GetDelWarning()};
	GetScreen()->ShowRemoveDlg(new C4GUI::ConfirmationDialog(warning.c_str(), LoadResStr(C4ResStrTableKey::IDS_BTN_DELETE),
		new C4GUI::CallbackHandlerExPar<C4StartupPlrSelDlg, ListItem *>(this, &C4StartupPlrSelDlg::OnDelBtnConfirm, pSel), C4GUI::MessageDialog::btnYesNo));
}

void C4StartupPlrSelDlg::OnDelBtnConfirm(ListItem *pSel)
{
	switch (eMode)
	{
	case PSDM_Player:
		if (!C4Group_DeleteItem(pSel->GetFilename().getData()))
		{
			StdStrBuf sMsg; sMsg.Copy(LoadResStr(C4ResStrTableKey::IDS_FAIL_DELETE));
			GetScreen()->ShowMessage(sMsg.getData(), LoadResStr(C4ResStrTableKey::IDS_DLG_CLEAR), C4GUI::Ico_Error);
		}
		break;

	case PSDM_Crew:
		if (!CurrPlayer.Grp.Delete(pSel->GetFilename().getData()))
		{
			StdStrBuf sMsg; sMsg.Copy(LoadResStr(C4ResStrTableKey::IDS_FAIL_DELETE));
			GetScreen()->ShowMessage(sMsg.getData(), LoadResStr(C4ResStrTableKey::IDS_DLG_CLEAR), C4GUI::Ico_Error);
		}
		break;
	}
	// update buttons 'n stuff
	UpdatePlayerList();
}

void C4StartupPlrSelDlg::SelectItem(const std::string &filename, bool fActivate)
{
	// find item
	for (ListItem *pPlrItem = static_cast<ListItem *>(pPlrListBox->GetFirst()); pPlrItem; pPlrItem = pPlrItem->GetNext())
		if (ItemIdentical(pPlrItem->GetFilename().getData(), filename.c_str()))
		{
			// select it
			pPlrListBox->SelectEntry(pPlrItem, false);
			// activate it
			if (fActivate)
			{
				pPlrItem->SetActivated(true);
				// player activation updates
				OnItemCheckChange(nullptr);
			}
			// max one
			return;
		}
}

void C4StartupPlrSelDlg::OnPropertyBtn(C4GUI::Control *btn)
{
	// something has changed!
	AbortRenaming();
	switch (eMode)
	{
	case PSDM_Player:
	{
		// show property dialog for selected player
		PlayerListItem *pSel = static_cast<PlayerListItem *>(GetSelection());
		if (!pSel) return;
		C4GUI::Dialog *pDlg;
		GetScreen()->ShowRemoveDlg(pDlg = new C4StartupPlrPropertiesDlg(pSel, this));
		pDlg->SetPos(std::min<int32_t>(GetBounds().Wdt / 10, GetBounds().Wdt - pDlg->GetBounds().Wdt),
			(GetBounds().Hgt - pDlg->GetBounds().Hgt) / 2);
	}
	break;

	case PSDM_Crew:
		// rename crew
		CrewListItem *pSel = static_cast<CrewListItem *>(GetSelection());
		if (!pSel) return;
		pSel->CrewRename();
		break;
	}
}

// Crew sorting

struct C4StartupPlrSelDlg_CrewSortDataEntry
{
	int32_t iMaxExp;
	C4ID idType;

	C4StartupPlrSelDlg_CrewSortDataEntry(int32_t iMaxExp, C4ID idType) : iMaxExp(iMaxExp), idType(idType) {}
};

class C4StartupPlrSelDlg_CrewSortDataMatchType
{
	C4ID idType;

public:
	C4StartupPlrSelDlg_CrewSortDataMatchType(C4ID idType) : idType(idType) {}
	bool operator()(C4StartupPlrSelDlg_CrewSortDataEntry Check) { return Check.idType == idType; }
};

typedef std::vector<C4StartupPlrSelDlg_CrewSortDataEntry> C4StartupPlrSelDlg_CrewSortData;

int32_t C4StartupPlrSelDlg::CrewSortFunc(const C4GUI::Element *pEl1, const C4GUI::Element *pEl2, void *par)
{
	const CrewListItem *pItem1 = static_cast<const CrewListItem *>(pEl1);
	const CrewListItem *pItem2 = static_cast<const CrewListItem *>(pEl2);
	C4StartupPlrSelDlg_CrewSortData &rSortData = *static_cast<C4StartupPlrSelDlg_CrewSortData *>(par);
	C4StartupPlrSelDlg_CrewSortData::iterator i = std::find_if(rSortData.begin(), rSortData.end(), C4StartupPlrSelDlg_CrewSortDataMatchType(pItem1->GetCore().id)),
		j = std::find_if(rSortData.begin(), rSortData.end(), C4StartupPlrSelDlg_CrewSortDataMatchType(pItem2->GetCore().id));
	// primary sort: By Clonk type, where high exp Clonk types are sorted atop low exp Clonk types
	if (i != j)
	{
		if (i == rSortData.end()) return -1; else if (j == rSortData.end()) return +1; // can't really happen
		return (*i).iMaxExp - (*j).iMaxExp;
	}
	// secondary: By experience
	return pItem1->GetCore().Experience - pItem2->GetCore().Experience;
}

void C4StartupPlrSelDlg::ResortCrew()
{
	assert(eMode == PSDM_Crew);
	// create a list of Clonk types and their respective maximum experience
	C4StartupPlrSelDlg_CrewSortData SortData;
	for (CrewListItem *pCrewItem = static_cast<CrewListItem *>(pPlrListBox->GetFirst()); pCrewItem; pCrewItem = pCrewItem->GetNext())
	{
		C4StartupPlrSelDlg_CrewSortData::iterator i = std::find_if(SortData.begin(), SortData.end(), C4StartupPlrSelDlg_CrewSortDataMatchType(pCrewItem->GetCore().id));
		if (i == SortData.end())
			SortData.push_back(C4StartupPlrSelDlg_CrewSortDataEntry(pCrewItem->GetCore().Experience, pCrewItem->GetCore().id));
		else
			(*i).iMaxExp = std::max<int32_t>((*i).iMaxExp, pCrewItem->GetCore().Experience);
	}
	pPlrListBox->SortElements(&CrewSortFunc, &SortData);
}

// Player property dlg

C4StartupPlrPropertiesDlg::C4StartupPlrPropertiesDlg(C4StartupPlrSelDlg::PlayerListItem *pForPlayer, C4StartupPlrSelDlg *pParentDlg)
	: Dialog(C4Startup::Get()->Graphics.fctPlrPropBG.Wdt, C4Startup::Get()->Graphics.fctPlrPropBG.Hgt, "", false), pForPlayer(pForPlayer), pMainDlg(pParentDlg),
	fClearPicture(false), fClearBigIcon(false)
{
	if (pForPlayer)
	{
		// edit existing player
		C4P = pForPlayer->GetCore();
	}
	else
	{
		// create new player: Use default C4P values, with a few exceptions
		// FIXME: Use Player, not Clonkranks
		C4P.Default(&Game.Rank);
		// Set name, color, comment
		SCopy(LoadResStr(C4ResStrTableKey::IDS_PLR_NEWCOMMENT), C4P.Comment, C4MaxComment);
		C4P.PrefColor = SafeRandom(8);
		C4P.PrefColorDw = C4P.GetPrefColorValue(C4P.PrefColor);
		C4P.PrefControlStyle = 1;
		C4P.PrefAutoContextMenu = 1;
		C4P.PrefControl = C4P_Control_Keyboard1;
	}
	const int32_t BetweenElementDist = 2;
	// use black fonts here
	CStdFont *pUseFont = &C4Startup::Get()->Graphics.BookFont;
	CStdFont *pSmallFont = &C4Startup::Get()->Graphics.BookSmallFont;
	// get positions
	UpdateSize();
	C4GUI::ComponentAligner caMain(GetClientRect(), 0, 1, true);
	C4GUI::ComponentAligner caButtonArea(caMain.GetFromBottom(C4GUI_ButtonAreaHgt), 0, 0);
	// dlg title
	const char *szTitle;
	if (pForPlayer)
	{
		szTitle = LoadResStr(C4ResStrTableKey::IDS_DLG_PLAYER2);
	}
	else
	{
		szTitle = LoadResStr(C4ResStrTableKey::IDS_PLR_NEWPLAYER);
	}
	C4GUI::Label *pLbl = new C4GUI::Label(szTitle, caMain.GetFromTop(pUseFont->GetLineHeight()), ALeft, C4StartupFontClr, pUseFont, false);
	AddElement(pLbl);
	caMain.ExpandTop(-BetweenElementDist);
	// place name label
	AddElement(new C4GUI::Label(LoadResStr(C4ResStrTableKey::IDS_CTL_NAME2), caMain.GetFromTop(pSmallFont->GetLineHeight()), ALeft, C4StartupFontClr, pSmallFont, false));
	// place name edit
	pNameEdit = new C4GUI::Edit(caMain.GetFromTop(C4GUI::Edit::GetCustomEditHeight(pUseFont)));
	pNameEdit->SetFont(pUseFont);
	pNameEdit->SetColors(C4StartupEditBGColor, C4StartupFontClr, C4StartupEditBorderColor);
	pNameEdit->InsertText(C4P.PrefName, false);
	pNameEdit->SetMaxText(C4MaxName);
	AddElement(pNameEdit);
	SetFocus(pNameEdit, false);
	caMain.ExpandTop(-BetweenElementDist);
	// place color label
	AddElement(new C4GUI::Label(std::format("{}:", LoadResStr(C4ResStrTableKey::IDS_CTL_COLOR)).c_str(), caMain.GetFromTop(pSmallFont->GetLineHeight()), ALeft, C4StartupFontClr, pSmallFont, false));
	// place color controls
	C4GUI::ComponentAligner caColorArea(caMain.GetFromTop(C4GUI::ArrowButton::GetDefaultHeight()), 2, 0);
	caColorArea.ExpandLeft(2);
	C4GUI::Button *pBtn; const char *szTip;
	szTip = LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERCOLORS);
	AddElement(pBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::ArrowButton>(C4GUI::ArrowButton::Left, caColorArea.GetFromLeft(C4GUI::ArrowButton::GetDefaultWidth()), &C4StartupPlrPropertiesDlg::OnClrChangeLeft));
	pBtn->SetToolTip(szTip);
	C4Facet &rfctClrPreviewPic = Game.GraphicsResource.fctFlagClr;
	pClrPreview = new C4GUI::Picture(caColorArea.GetFromLeft(rfctClrPreviewPic.GetWidthByHeight(caColorArea.GetHeight())), true);
	pClrPreview->SetFacet(rfctClrPreviewPic);
	AddElement(pClrPreview);
	AddElement(pBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::ArrowButton>(C4GUI::ArrowButton::Right, caColorArea.GetFromLeft(C4GUI::ArrowButton::GetDefaultWidth()), &C4StartupPlrPropertiesDlg::OnClrChangeRight));
	pBtn->SetToolTip(szTip);
	szTip = LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERCOLORSTGB);
	int32_t iSliderYDiff = (caColorArea.GetHeight() - 3 * C4GUI_ScrollBarHgt) / 2;
	pClrSliderR = new C4GUI::ScrollBar(caColorArea.GetFromTop(C4GUI_ScrollBarHgt), true, new C4GUI::ParCallbackHandler<C4StartupPlrPropertiesDlg, int32_t>(this, &C4StartupPlrPropertiesDlg::OnClrSliderRChange));
	pClrSliderR->SetDecoration(&C4Startup::Get()->Graphics.sfctBookScrollR, false);
	pClrSliderR->SetToolTip(szTip);
	caColorArea.ExpandTop(-iSliderYDiff);
	pClrSliderG = new C4GUI::ScrollBar(caColorArea.GetFromTop(C4GUI_ScrollBarHgt), true, new C4GUI::ParCallbackHandler<C4StartupPlrPropertiesDlg, int32_t>(this, &C4StartupPlrPropertiesDlg::OnClrSliderGChange));
	pClrSliderG->SetDecoration(&C4Startup::Get()->Graphics.sfctBookScrollG, false);
	pClrSliderG->SetToolTip(szTip);
	caColorArea.ExpandTop(-iSliderYDiff);
	pClrSliderB = new C4GUI::ScrollBar(caColorArea.GetFromTop(C4GUI_ScrollBarHgt), true, new C4GUI::ParCallbackHandler<C4StartupPlrPropertiesDlg, int32_t>(this, &C4StartupPlrPropertiesDlg::OnClrSliderBChange));
	pClrSliderB->SetDecoration(&C4Startup::Get()->Graphics.sfctBookScrollB, false);
	pClrSliderB->SetToolTip(szTip);
	AddElement(pClrSliderR);
	AddElement(pClrSliderG);
	AddElement(pClrSliderB);
	if (!C4P.PrefColorDw) C4P.PrefColorDw = 0xff;
	caMain.ExpandTop(-BetweenElementDist);
	// place control and picture label
	int32_t iControlPicSize = C4GUI::ArrowButton::GetDefaultHeight();
	C4GUI::ComponentAligner caControlArea(caMain.GetFromTop(iControlPicSize + pSmallFont->GetLineHeight() + BetweenElementDist), 0, 0, false);
	C4GUI::ComponentAligner caPictureArea(caControlArea.GetFromRight(iControlPicSize), 0, 0, false);
	AddElement(new C4GUI::Label(std::format("{}:", LoadResStr(C4ResStrTableKey::IDS_CTL_CONTROL)).c_str(), caControlArea.GetFromTop(pSmallFont->GetLineHeight()), ALeft, C4StartupFontClr, pSmallFont, false));
	AddElement(new C4GUI::Label(LoadResStr(C4ResStrTableKey::IDS_CTL_PICTURE), caPictureArea.GetFromTop(pSmallFont->GetLineHeight()), ACenter, C4StartupFontClr, pSmallFont, false));
	caControlArea.ExpandTop(-BetweenElementDist); caPictureArea.ExpandTop(-BetweenElementDist);
	// place control controls
	C4GUI::ComponentAligner caControl(caControlArea.GetFromTop(iControlPicSize), 2, 0);
	szTip = LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERCONTROL);
	AddElement(pBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::ArrowButton>(C4GUI::ArrowButton::Left, caControl.GetFromLeft(C4GUI::ArrowButton::GetDefaultWidth()), &C4StartupPlrPropertiesDlg::OnCtrlChangeLeft));
	pBtn->SetToolTip(szTip);
	C4Facet &rfctCtrlPic = Game.GraphicsResource.fctKeyboard; // UpdatePlayerControl() will alternatively set fctGamepad
	AddElement(pCtrlImg = new C4GUI::Picture(caControl.GetFromLeft(rfctCtrlPic.GetWidthByHeight(caControl.GetHeight())), true));
	pCtrlImg->SetToolTip(szTip);
	AddElement(pBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::ArrowButton>(C4GUI::ArrowButton::Right, caControl.GetFromLeft(C4GUI::ArrowButton::GetDefaultWidth()), &C4StartupPlrPropertiesDlg::OnCtrlChangeRight));
	pBtn->SetToolTip(szTip);
	caControl.ExpandLeft(-10);
	AddElement(pMouseBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::IconButton>(C4GUI::Ico_MouseOff, caControl.GetFromLeft(caControl.GetHeight()), 'M' /* 2do */, &C4StartupPlrPropertiesDlg::OnCtrlChangeMouse));
	pMouseBtn->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DLGTIP_PLAYERCONTROLMOUSE));
	C4P.PrefControl = BoundBy<int32_t>(C4P.PrefControl, 0, C4MaxControlSet - 1);
	UpdatePlayerControl();
	// place picture button
	AddElement(pPictureBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::IconButton>(C4GUI::Ico_Player, caPictureArea.GetAll(), 'P' /* 2do */, &C4StartupPlrPropertiesDlg::OnPictureBtn));
	pPictureBtn->SetToolTip(LoadResStr(C4ResStrTableKey::IDS_DESC_SELECTAPICTUREANDORLOBBYI));
	UpdateBigIcon();
	UpdatePlayerColor(true);
	caMain.ExpandTop(-BetweenElementDist);
	// place AutoStopControl label
	AddElement(new C4GUI::Label(std::format("{}:", LoadResStr(C4ResStrTableKey::IDS_DLG_MOVEMENT)).c_str(), caMain.GetFromTop(pSmallFont->GetLineHeight()), ALeft, C4StartupFontClr, pSmallFont, false));
	// place AutoStopControl controls
	C4Facet &rfctMovementIcons = C4Startup::Get()->Graphics.fctPlrCtrlType;
	C4GUI::ComponentAligner caMovement(caMain.GetFromTop(rfctMovementIcons.Hgt), 5, 0);
	C4Rect rcBtn = caMovement.GetFromLeft(rfctMovementIcons.GetWidthByHeight(caMovement.GetHeight()));
	AddElement(pLbl = new C4GUI::Label(LoadResStr(C4ResStrTableKey::IDS_DLG_JUMPANDRUN), rcBtn.x + rcBtn.Wdt / 2, rcBtn.y + rcBtn.Hgt - 6, ACenter, C4StartupFontClr, pSmallFont, false));
	szTip = LoadResStr(C4ResStrTableKey::IDS_DLGTIP_JUMPANDRUN);
	pLbl->SetToolTip(szTip);
	AddElement(pJumpNRunBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::IconButton>(C4GUI::Ico_None, rcBtn, 'J' /* 2do */, &C4StartupPlrPropertiesDlg::OnMovementBtn));
	pJumpNRunBtn->SetToolTip(szTip);
	rcBtn = caMovement.GetFromRight(rfctMovementIcons.GetWidthByHeight(caMovement.GetHeight()));
	AddElement(pLbl = new C4GUI::Label(LoadResStr(C4ResStrTableKey::IDS_DLG_CLASSIC), rcBtn.x + rcBtn.Wdt / 2, rcBtn.y + rcBtn.Hgt - 6, ACenter, C4StartupFontClr, pSmallFont, false));
	szTip = LoadResStr(C4ResStrTableKey::IDS_DLGTIP_CLASSIC);
	pLbl->SetToolTip(szTip);
	AddElement(pClassicBtn = new C4GUI::CallbackButton<C4StartupPlrPropertiesDlg, C4GUI::IconButton>(C4GUI::Ico_None, rcBtn, 'C' /* 2do */, &C4StartupPlrPropertiesDlg::OnMovementBtn));
	pClassicBtn->SetToolTip(szTip);
	UpdatePlayerMovement();
	// place buttons
	// OK
	C4GUI::Button *pBtnOK = C4GUI::newOKIconButton(C4Rect(147 - GetMarginLeft(), 295 + 35 - GetMarginTop(), 54, 33), C4GUI::Ico_None);
	AddElement(pBtnOK);
	// Cancel
	C4GUI::Button *pBtnAbort = C4GUI::newCancelIconButton(C4Rect(317 - GetMarginLeft(), 16 - GetMarginTop(), 21, 21), C4GUI::Ico_None);
	AddElement(pBtnAbort);
	// New player
	if (!pForPlayer)
	{
		// Set initial portrait and bigicon
		C4Group hGroup;
		const std::string portrait{std::format("Portrait{}.png", 1 + Random(5))};
		if (hGroup.Open(Config.AtExePath(C4CFN_Graphics)))
		{
			hGroup.Extract(portrait.c_str(), Config.AtTempPath("Portrait.png"));
			hGroup.Close();
			SetNewPicture(Config.AtTempPath("Portrait.png"), true, true);
			EraseItem(Config.AtTempPath("Portrait.png"));
		}
	}
	// when called from player selection screen: input dlg always closed in the end
	// otherwise, modal proc will delete
	if (pMainDlg) SetDelOnClose();
}

void C4StartupPlrPropertiesDlg::DrawElement(C4FacetEx &cgo)
{
	C4Startup::Get()->Graphics.fctPlrPropBG.Draw(cgo.Surface, rcBounds.x + cgo.TargetX, rcBounds.y + cgo.TargetY);
}

bool IsColorConflict(uint32_t dwClr1, uint32_t dwClr2);

void C4StartupPlrPropertiesDlg::UpdatePlayerColor(bool fUpdateSliders)
{
	if (!C4P.PrefColorDw) C4P.PrefColorDw = 1; // no black! Would turn to blue in some instances
	pClrPreview->SetDrawColor(C4P.PrefColorDw);
	pPictureBtn->SetColor(C4P.PrefColorDw);
	if (fUpdateSliders)
	{
		pClrSliderR->SetScrollPos((C4P.PrefColorDw >> 16) & 0xff);
		pClrSliderG->SetScrollPos((C4P.PrefColorDw >> 8) & 0xff);
		pClrSliderB->SetScrollPos(C4P.PrefColorDw & 0xff);
	}
}

void C4StartupPlrPropertiesDlg::OnClrChangeLeft(C4GUI::Control *pBtn)
{
	// previous standard color in list
	C4P.PrefColor = C4P.PrefColor ? C4P.PrefColor - 1 : 11;
	C4P.PrefColorDw = C4PlayerInfoCore::GetPrefColorValue(C4P.PrefColor);
	UpdatePlayerColor(true);
}

void C4StartupPlrPropertiesDlg::OnClrChangeRight(C4GUI::Control *pBtn)
{
	// next standard color in list
	C4P.PrefColor = (C4P.PrefColor + 1) % 12;
	C4P.PrefColorDw = C4PlayerInfoCore::GetPrefColorValue(C4P.PrefColor);
	UpdatePlayerColor(true);
}

void C4StartupPlrPropertiesDlg::OnClrSliderRChange(int32_t iNewVal)
{
	// update red component of color
	C4P.PrefColorDw = (C4P.PrefColorDw & 0xffff) + (iNewVal << 16);
	UpdatePlayerColor(false);
}

void C4StartupPlrPropertiesDlg::OnClrSliderGChange(int32_t iNewVal)
{
	// update green component of color
	C4P.PrefColorDw = (C4P.PrefColorDw & 0xff00ff) + (iNewVal << 8);
	UpdatePlayerColor(false);
}

void C4StartupPlrPropertiesDlg::OnClrSliderBChange(int32_t iNewVal)
{
	// update blue component of color
	C4P.PrefColorDw = (C4P.PrefColorDw & 0xffff00) + iNewVal;
	UpdatePlayerColor(false);
}

void C4StartupPlrPropertiesDlg::UpdatePlayerControl()
{
	// update keyboard image of selected control
	C4Facet &rfctCtrlPic = (C4P.PrefControl < C4P_Control_GamePad1) ? Game.GraphicsResource.fctKeyboard : Game.GraphicsResource.fctGamepad;
	pCtrlImg->SetFacet(rfctCtrlPic);
	pCtrlImg->GetMFacet().X += rfctCtrlPic.Wdt * (C4P.PrefControl - ((C4P.PrefControl < C4P_Control_GamePad1) ? 0 : C4P_Control_GamePad1));
	// update mouse image
	pMouseBtn->SetIcon(C4P.PrefMouse ? C4GUI::Ico_MouseOn : C4GUI::Ico_MouseOff);
}

void C4StartupPlrPropertiesDlg::OnCtrlChangeLeft(C4GUI::Control *pBtn)
{
	// previous control set in list
	C4P.PrefControl = C4P.PrefControl ? C4P.PrefControl - 1 : C4MaxControlSet - 1;
	UpdatePlayerControl();
}

void C4StartupPlrPropertiesDlg::OnCtrlChangeRight(C4GUI::Control *pBtn)
{
	// next control set in list
	C4P.PrefControl = (C4P.PrefControl + 1) % C4MaxControlSet;
	UpdatePlayerControl();
}

void C4StartupPlrPropertiesDlg::OnCtrlChangeMouse(C4GUI::Control *pBtn)
{
	// toggle mouse usage
	C4P.PrefMouse = !C4P.PrefMouse;
	UpdatePlayerControl();
}

void C4StartupPlrPropertiesDlg::UpdatePlayerMovement()
{
	// hightlight to control tyope that is selected
	pJumpNRunBtn->SetFacet(C4Startup::Get()->Graphics.fctPlrCtrlType.GetPhase(C4P.PrefControlStyle ? 1 : 0, 1));
	pClassicBtn->SetFacet(C4Startup::Get()->Graphics.fctPlrCtrlType.GetPhase(C4P.PrefControlStyle ? 0 : 1, 0));
}

void C4StartupPlrPropertiesDlg::OnMovementBtn(C4GUI::Control *pBtn)
{
	// Set new control style
	C4P.PrefControlStyle = (pBtn == pJumpNRunBtn);
	// Adjust pref for AutoContextMenus along with control style
	C4P.PrefAutoContextMenu = C4P.PrefControlStyle;
	// Update dialog
	UpdatePlayerMovement();
}

void C4StartupPlrPropertiesDlg::UserClose(bool fOK)
{
	// check name validity
	if (fOK)
	{
		StdStrBuf PlrName(pNameEdit->GetText(), false);
		std::string filename;
		if (!C4StartupPlrSelDlg::CheckPlayerName(PlrName, filename, pForPlayer ? &pForPlayer->GetFilename() : nullptr, true)) return;
	}
	Close(fOK);
}

void C4StartupPlrPropertiesDlg::OnClosed(bool fOK)
{
	if (fOK)
	{
		// store selected data if desired
		StdStrBuf PlrName(pNameEdit->GetText(), false);
		std::string filename;
		if (C4StartupPlrSelDlg::CheckPlayerName(PlrName, filename, pForPlayer ? &pForPlayer->GetFilename() : nullptr, true))
		{
			SCopy(PlrName.getData(), C4P.PrefName, C4MaxName);
			C4Group PlrGroup;
			bool fSucc = false;
			// existent player: update file
			if (pForPlayer)
			{
				if (!pForPlayer->MoveFilename(filename.c_str()))
					GetScreen()->ShowMessage(LoadResStr(C4ResStrTableKey::IDS_FAIL_RENAME), "", C4GUI::Ico_Error);
				// update picture/bigicon
				if (fClearPicture || fClearBigIcon || fctNewPicture.Surface || fctNewBigIcon.Surface)
				{
					C4Group PlrGroup;
					if (PlrGroup.Open(filename.c_str()))
					{
						if (fClearPicture || fctNewPicture.Surface) PlrGroup.Delete(C4CFN_Portrait);
						if (fClearBigIcon || fctNewBigIcon.Surface) PlrGroup.Delete(C4CFN_BigIcon);
						if (fctNewPicture.Surface) fctNewPicture.GetFace().SavePNG(PlrGroup, C4CFN_Portrait);
						if (fctNewBigIcon.Surface) fctNewBigIcon.GetFace().SavePNG(PlrGroup, C4CFN_BigIcon);
						if (PlrGroup.Close()) fSucc = true;
						if (fClearBigIcon || fctNewBigIcon.Surface) pForPlayer->GrabCustomIcon(fctNewBigIcon);
						if (fClearPicture || fctNewPicture.Surface) pForPlayer->GrabPortrait(&fctNewPicture);
					}
				}
				else
				{
					fSucc = true;
				}
				pForPlayer->UpdateCore(C4P);
				// player may have been activated: Make sure any new filename is reflected in participants list
				if (pMainDlg) pMainDlg->UpdateActivatedPlayers();
			}
			else
			{
				// NewPlayer: Open new player group
				if (PlrGroup.Open(filename.c_str(), true))
				{
					// Do not overwrite (should have been caught earlier anyway)
					if (PlrGroup.FindEntry(C4CFN_PlayerInfoCore)) return;
					// Save info core
					C4P.Save(PlrGroup);
					// Add portrait
					if (fctNewPicture.Surface)
					{
						fctNewPicture.GetFace().SavePNG(PlrGroup, C4CFN_Portrait);
					}
					else if (!fClearPicture)
					{
						// default picture
						char *pBytes; size_t iSize;
						if (GetPortrait(&pBytes, &iSize))
						{
							PlrGroup.Add(C4CFN_Portrait, pBytes, iSize, false, true);
						}
					}
					// Add BigIcon
					if (fctNewBigIcon.Surface)
					{
						fctNewBigIcon.GetFace().SavePNG(PlrGroup, C4CFN_BigIcon);
					}
					// Close group
					if (PlrGroup.Close()) fSucc = true;
					// update activate button text
					if (pMainDlg)
					{
						pMainDlg->UpdatePlayerList();
						pMainDlg->SelectItem(filename, true);
					}
					else
					{
						// no main player selection dialog: This means that this dlg was shown as a creation dialog from the main startup dlg
						// Just set the newly created player as current selection
						SCopy(Config.AtExeRelativePath(filename.c_str()), Config.General.Participants, sizeof(Config.General.Participants));
					}
				}
			}
			if (!fSucc) GetScreen()->ShowErrorMessage(PlrGroup.GetError());
		}
	}
	// Make the dialog go away
	Dialog::OnClosed(fOK);
}

bool C4StartupPlrPropertiesDlg::SetNewPicture(C4Surface &srcSfc, C4FacetExSurface *trgFct, int32_t iMaxSize, bool fColorize)
{
	if (fColorize)
	{
		C4Surface srcSfcClr;
		if (!srcSfcClr.CreateColorByOwner(&srcSfc)) return false;
		return trgFct->CopyFromSfcMaxSize(srcSfcClr, iMaxSize, C4P.PrefColorDw);
	}
	else
	{
		return trgFct->CopyFromSfcMaxSize(srcSfc, iMaxSize);
	}
}

void C4StartupPlrPropertiesDlg::SetNewPicture(const char *szFromFilename, bool fSetPicture, bool fSetBigIcon)
{
	if (!szFromFilename)
	{
		// If szFromFilename==nullptr, clear picture/bigicon
		if (fSetPicture) { fClearPicture = true; fctNewPicture.Clear(); }
		if (fSetBigIcon) { fClearBigIcon = true; fctNewBigIcon.Clear(); }
	}
	else if (fSetPicture || fSetBigIcon)
	{
		// else set new picture/bigicon by loading and scaling if necessary.
		C4Surface sfcNewPic;
		C4Group SrcGrp;
		StdStrBuf sParentPath;
		GetParentPath(szFromFilename, &sParentPath);
		bool fSucc = false;
		if (SrcGrp.Open(sParentPath.getData()))
		{
			if (sfcNewPic.Load(SrcGrp, GetFilename(szFromFilename)))
			{
				fSucc = true;
				if (fSetPicture) if (!SetNewPicture(sfcNewPic, &fctNewPicture, C4MaxPictureSize, false)) fSucc = false;
				if (fSetBigIcon) if (!SetNewPicture(sfcNewPic, &fctNewBigIcon, C4MaxBigIconSize, true)) fSucc = false;
			}
		}
		if (!fSucc)
		{
			// error!
			GetScreen()->ShowErrorMessage(LoadResStr(C4ResStrTableKey::IDS_PRC_NOGFXFILE, szFromFilename, SrcGrp.GetError()).c_str());
		}
	}
	// update icon
	if (fSetBigIcon) UpdateBigIcon();
}

void C4StartupPlrPropertiesDlg::OnPictureBtn(C4GUI::Control *pBtn)
{
	std::string newPic;
	bool setPicture = true;
	bool setBigIcon = true;
	if (C4PortraitSelDlg::SelectPortrait(GetScreen(), newPic, &setPicture, &setBigIcon))
	{
		SetNewPicture(newPic.c_str(), setPicture, setBigIcon);
	}
}

void C4StartupPlrPropertiesDlg::UpdateBigIcon()
{
	// new icon?
	bool fHasIcon = false;
	if (fctNewBigIcon.Surface)
	{
		pPictureBtn->SetFacet(fctNewBigIcon);
		fHasIcon = true;
	}
	// old icon in existing player?
	else if (!fClearBigIcon && pForPlayer)
	{
		C4Group PlrGroup;
		if (PlrGroup.Open(pForPlayer->GetFilename().getData()))
		{
			if (PlrGroup.FindEntry(C4CFN_BigIcon))
			{
				if (fctOldBigIcon.Load(PlrGroup, C4CFN_BigIcon))
				{
					pPictureBtn->SetFacet(fctOldBigIcon);
					fHasIcon = true;
				}
			}
		}
	}
	// no icon: Set default
	if (!fHasIcon)
	{
		pPictureBtn->SetFacet(Game.GraphicsResource.fctPlayerClr);
	}
}
