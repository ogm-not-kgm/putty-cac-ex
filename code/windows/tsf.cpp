#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <atlbase.h>
#include <olectl.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "tsf.h"

namespace {

constexpr TsViewCookie kViewCookie = 1;
constexpr int kContextBufMax = 256;

class TsfSink;
class TsfTextStore;

bool ReadCompartmentBool(ITfCompartment *comp, bool fallback) {
    if (!comp)
        return fallback;
    VARIANT var;
    VariantInit(&var);
    bool result = fallback;
    if (SUCCEEDED(comp->GetValue(&var))) {
        if (var.vt == VT_I4)
            result = (var.lVal != 0);
        else if (var.vt == VT_EMPTY)
            result = false;
        VariantClear(&var);
    }
    return result;
}

template <class SinkInterface>
bool AdviseSinkHelper(IUnknown *source_unk, SinkInterface *sink_impl,
                      DWORD *cookie) {
    if (!source_unk || !sink_impl || !cookie)
        return false;
    CComPtr<ITfSource> source;
    if (FAILED(source_unk->QueryInterface(IID_PPV_ARGS(&source))) || !source)
        return false;
    return SUCCEEDED(source->AdviseSink(__uuidof(SinkInterface), sink_impl,
                                        cookie)) &&
           *cookie != TF_INVALID_COOKIE;
}

void UnadviseSinkHelper(IUnknown *source_unk, DWORD *cookie) {
    if (!source_unk || !cookie || *cookie == TF_INVALID_COOKIE)
        return;
    CComPtr<ITfSource> source;
    if (SUCCEEDED(source_unk->QueryInterface(IID_PPV_ARGS(&source))) && source) {
        source->UnadviseSink(*cookie);
    }
    *cookie = TF_INVALID_COOKIE;
}

bool GetRangeExtent(ITfCompositionView *comp_view, LONG *start_out,
                    LONG *len_out) {
    if (!comp_view || !start_out || !len_out)
        return false;
    CComPtr<ITfRange> range;
    if (FAILED(comp_view->GetRange(&range)) || !range)
        return false;
    CComPtr<ITfRangeACP> range_acp;
    if (FAILED(range->QueryInterface(IID_PPV_ARGS(&range_acp))) || !range_acp)
        return false;
    return SUCCEEDED(range_acp->GetExtent(start_out, len_out));
}

class TsfSink final : public ITfThreadMgrEventSink,
                      public ITfCompartmentEventSink {
  public:
    explicit TsfSink(TsfState *owner) : ref_count_(1), owner_(owner) {}

    void Detach() { owner_ = nullptr; }

    /* IUnknown */
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
            return 0;
        }
        return static_cast<ULONG>(count);
    }

    /* ITfThreadMgrEventSink */
    IFACEMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pdim) override {
        return S_OK;
    }
    IFACEMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pdim) override {
        return S_OK;
    }
    IFACEMETHODIMP OnSetFocus(ITfDocumentMgr *pdimFocus,
                              ITfDocumentMgr *pdimPrevFocus) override;
    IFACEMETHODIMP OnPushContext(ITfContext *pic) override { return S_OK; }
    IFACEMETHODIMP OnPopContext(ITfContext *pic) override { return S_OK; }

    /* ITfCompartmentEventSink */
    IFACEMETHODIMP OnChange(REFGUID rguid) override;

  private:
    ~TsfSink() = default;

    LONG ref_count_;
    TsfState *owner_;
};

class TsfTextStore final : public ITextStoreACP,
                           public ITfContextOwnerCompositionSink,
                           public ITfTextEditSink {
  public:
    explicit TsfTextStore(TsfState *owner) : ref_count_(1), owner_(owner) {}

    void Detach() {
        owner_ = nullptr;
        active_comp_.Release();
        text_sink_.Release();
    }

    bool IsInLock() const { return lock_type_ != 0; }
    bool IsCompActive() const { return comp_active_; }
    void RefreshContext(bool notify_sink);
    void NotifyLayoutChanged();

    /* IUnknown */
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
            return 0;
        }
        return static_cast<ULONG>(count);
    }

    /* ITextStoreACP */
    IFACEMETHODIMP AdviseSink(REFIID riid, IUnknown *punk,
                              DWORD dwMask) override;
    IFACEMETHODIMP UnadviseSink(IUnknown *punk) override;
    IFACEMETHODIMP RequestLock(DWORD dwLockFlags, HRESULT *phrSession) override;
    IFACEMETHODIMP GetStatus(TS_STATUS *pdcs) override;
    IFACEMETHODIMP QueryInsert(LONG acpTestStart, LONG acpTestEnd, ULONG cch,
                               LONG *pacpResultStart,
                               LONG *pacpResultEnd) override;
    IFACEMETHODIMP GetSelection(ULONG ulIndex, ULONG ulCount,
                                TS_SELECTION_ACP *pSelection,
                                ULONG *pcFetched) override;
    IFACEMETHODIMP SetSelection(ULONG ulCount,
                                const TS_SELECTION_ACP *pSelection) override;
    IFACEMETHODIMP GetText(LONG acpStart, LONG acpEnd, WCHAR *pchPlain,
                           ULONG cchPlainReq, ULONG *pcchPlainRet,
                           TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq,
                           ULONG *pcRunInfoRet, LONG *pacpNext) override;
    IFACEMETHODIMP SetText(DWORD dwFlags, LONG acpStart, LONG acpEnd,
                           const WCHAR *pchText, ULONG cch,
                           TS_TEXTCHANGE *pChange) override;
    IFACEMETHODIMP GetFormattedText(LONG acpStart, LONG acpEnd,
                                    IDataObject **ppDataObject) override {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP GetEmbedded(LONG acpPos, REFGUID rguidService, REFIID riid,
                               IUnknown **ppunk) override {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP QueryInsertEmbedded(const GUID *pguidService,
                                       const FORMATETC *pFormatEtc,
                                       BOOL *pfInsertable) override {
        if (!pfInsertable)
            return E_INVALIDARG;
        *pfInsertable = FALSE;
        return S_OK;
    }
    IFACEMETHODIMP InsertEmbedded(DWORD dwFlags, LONG acpStart, LONG acpEnd,
                                  IDataObject *pDataObject,
                                  TS_TEXTCHANGE *pChange) override {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP InsertTextAtSelection(DWORD dwFlags, const WCHAR *pchText,
                                         ULONG cch, LONG *pacpStart,
                                         LONG *pacpEnd,
                                         TS_TEXTCHANGE *pChange) override;
    IFACEMETHODIMP InsertEmbeddedAtSelection(DWORD dwFlags,
                                             IDataObject *pDataObject,
                                             LONG *pacpStart, LONG *pacpEnd,
                                             TS_TEXTCHANGE *pChange) override {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP RequestSupportedAttrs(DWORD dwFlags, ULONG cFilterAttrs,
                                         const TS_ATTRID *paFilterAttrs) override {
        return S_OK;
    }
    IFACEMETHODIMP RequestAttrsAtPosition(LONG acpPos, ULONG cFilterAttrs,
                                          const TS_ATTRID *paFilterAttrs,
                                          DWORD dwFlags) override {
        return S_OK;
    }
    IFACEMETHODIMP RequestAttrsTransitioningAtPosition(
        LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs,
        DWORD dwFlags) override {
        return S_OK;
    }
    IFACEMETHODIMP FindNextAttrTransition(LONG acpStart, LONG acpHalt,
                                          ULONG cFilterAttrs,
                                          const TS_ATTRID *paFilterAttrs,
                                          DWORD dwFlags, LONG *pacpNext,
                                          BOOL *pfFound,
                                          LONG *plFoundOffset) override {
        if (pacpNext)
            *pacpNext = acpHalt;
        if (pfFound)
            *pfFound = FALSE;
        if (plFoundOffset)
            *plFoundOffset = 0;
        return S_OK;
    }
    IFACEMETHODIMP RetrieveRequestedAttrs(ULONG ulCount, TS_ATTRVAL *paAttrVals,
                                          ULONG *pcFetched) override {
        if (!pcFetched)
            return E_INVALIDARG;
        *pcFetched = 0;
        return S_OK;
    }
    IFACEMETHODIMP GetEndACP(LONG *pacp) override;
    IFACEMETHODIMP GetActiveView(TsViewCookie *pvcView) override;
    IFACEMETHODIMP GetACPFromPoint(TsViewCookie vcView, const POINT *ptScreen,
                                   DWORD dwFlags, LONG *pacp) override;
    IFACEMETHODIMP GetTextExt(TsViewCookie vcView, LONG acpStart, LONG acpEnd,
                              RECT *prc, BOOL *pfClipped) override;
    IFACEMETHODIMP GetScreenExt(TsViewCookie vcView, RECT *prc) override;
    IFACEMETHODIMP GetWnd(TsViewCookie vcView, HWND *phwnd) override;

    /* ITfContextOwnerCompositionSink */
    IFACEMETHODIMP OnStartComposition(ITfCompositionView *pComposition,
                                      BOOL *pfOk) override;
    IFACEMETHODIMP OnUpdateComposition(ITfCompositionView *pComposition,
                                       ITfRange *pRangeNew) override;
    IFACEMETHODIMP OnEndComposition(ITfCompositionView *pComposition) override;

    /* ITfTextEditSink */
    IFACEMETHODIMP OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly,
                             ITfEditRecord *pEditRecord) override;

  private:
    ~TsfTextStore() = default;

    bool IsLocked(DWORD flags) const { return (lock_type_ & flags) == flags; }
    int MeasureCells(LONG start_acp, LONG end_acp) const;
    void CommitTextSlice(LONG start_acp, LONG end_acp);
    void UpdatePreeditStyles(ITfContext *pic, TfEditCookie ecReadOnly);
    void EmitPreedit();
    void FlushDeferredNotifications();

    LONG ref_count_;
    TsfState *owner_;

    CComPtr<ITextStoreACPSink> text_sink_;
    DWORD text_sink_mask_ = 0;

    DWORD lock_type_ = 0;
    DWORD pending_lock_flags_ = 0;
    bool pending_layout_change_ = false;
    bool pending_context_refresh_ = false;

    std::wstring doc_text_;
    LONG sel_start_ = 0;
    LONG sel_end_ = 0;
    TsActiveSelEnd sel_ase_ = TS_AE_END;

    CComPtr<ITfCompositionView> active_comp_;
    bool comp_active_ = false;
    LONG comp_start_ = 0;
    LONG comp_end_ = 0;
    std::vector<uint8_t> comp_styles_;
    std::wstring pending_direct_text_;
};

} /* namespace */

struct TsfState {
    HWND hwnd = nullptr;
    void *host_ctx = nullptr;
    TsfHostCallbacks callbacks{};

    CComPtr<ITfThreadMgr> thread_mgr;
    CComPtr<ITfDocumentMgr> doc_mgr;
    CComPtr<ITfDocumentMgr> prev_doc_mgr;
    CComPtr<ITfContext> context;
    CComPtr<ITfKeystrokeMgr> keystroke_mgr;
    CComPtr<ITfCompartment> open_close_comp;
    CComPtr<ITfCategoryMgr> category_mgr;
    CComPtr<ITfDisplayAttributeMgr> display_attr_mgr;

    TfClientId client_id = TF_CLIENTID_NULL;
    TfEditCookie edit_cookie = TF_INVALID_EDIT_COOKIE;
    DWORD thread_mgr_sink_cookie = TF_INVALID_COOKIE;
    DWORD open_close_sink_cookie = TF_INVALID_COOKIE;
    DWORD edit_sink_cookie = TF_INVALID_COOKIE;

    TsfSink *sink = nullptr;
    TsfTextStore *store = nullptr;

    bool active = false;
    bool composition_active = false;
    bool ime_open = false;
};

namespace {

void SyncImeOpenState(TsfState *state, bool force_notify) {
    if (!state || !state->active)
        return;
    bool open = ReadCompartmentBool(state->open_close_comp, state->ime_open);
    if (force_notify || open != state->ime_open) {
        state->ime_open = open;
        if (state->callbacks.on_ime_mode_changed)
            state->callbacks.on_ime_mode_changed(state->host_ctx, open);
    }
}

/* ---------- TsfSink ---------- */

IFACEMETHODIMP TsfSink::QueryInterface(REFIID riid, void **ppv) {
    if (!ppv)
        return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppv = static_cast<ITfThreadMgrEventSink *>(this);
    } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
        *ppv = static_cast<ITfCompartmentEventSink *>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP TsfSink::OnSetFocus(ITfDocumentMgr * /*pdimFocus*/,
                                   ITfDocumentMgr * /*pdimPrevFocus*/) {
    if (owner_)
        SyncImeOpenState(owner_, false);
    return S_OK;
}

IFACEMETHODIMP TsfSink::OnChange(REFGUID rguid) {
    if (owner_ && IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
        SyncImeOpenState(owner_, false);
    return S_OK;
}

/* ---------- TsfTextStore helpers ---------- */

int TsfTextStore::MeasureCells(LONG start_acp, LONG end_acp) const {
    LONG doc_len = static_cast<LONG>(doc_text_.size());
    LONG s = std::clamp<LONG>(start_acp, 0, doc_len);
    LONG e = std::clamp<LONG>(end_acp, s, doc_len);
    if (e <= s)
        return 0;
    if (owner_ && owner_->callbacks.measure_text_cells) {
        return owner_->callbacks.measure_text_cells(
            owner_->host_ctx, doc_text_.data() + s, static_cast<int>(e - s));
    }
    return static_cast<int>(e - s);
}

void TsfTextStore::CommitTextSlice(LONG start_acp, LONG end_acp) {
    if (!owner_ || !owner_->callbacks.on_commit_text)
        return;
    LONG doc_len = static_cast<LONG>(doc_text_.size());
    LONG s = std::clamp<LONG>(start_acp, 0, doc_len);
    LONG e = std::clamp<LONG>(end_acp, s, doc_len);
    if (e > s) {
        owner_->callbacks.on_commit_text(
            owner_->host_ctx, doc_text_.data() + s, static_cast<int>(e - s));
    }
}

static uint8_t MapDisplayAttributeStyle(const TF_DISPLAYATTRIBUTE &da) {
    if (da.bAttr == TF_ATTR_TARGET_CONVERTED ||
        da.bAttr == TF_ATTR_TARGET_NOTCONVERTED || da.fBoldLine) {
        return TSF_PREEDIT_TARGET;
    }
    if (da.lsStyle == TF_LS_SOLID || da.bAttr == TF_ATTR_CONVERTED ||
        da.bAttr == TF_ATTR_FIXEDCONVERTED) {
        return TSF_PREEDIT_SOLID;
    }
    return TSF_PREEDIT_DOT;
}

void TsfTextStore::UpdatePreeditStyles(ITfContext *pic,
                                       TfEditCookie ecReadOnly) {
    LONG comp_len =
        comp_active_ ? std::max<LONG>(0, comp_end_ - comp_start_) : 0;
    if (comp_len == 0) {
        comp_styles_.clear();
        return;
    }
    comp_styles_.assign(static_cast<size_t>(comp_len), TSF_PREEDIT_DOT);

    if (!owner_ || !pic || !active_comp_ || !owner_->category_mgr ||
        !owner_->display_attr_mgr) {
        return;
    }

    CComPtr<ITfRange> comp_range;
    if (FAILED(active_comp_->GetRange(&comp_range)) || !comp_range)
        return;

    CComPtr<ITfProperty> prop;
    if (FAILED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop)) || !prop)
        return;

    CComPtr<IEnumTfRanges> enum_ranges;
    if (FAILED(prop->EnumRanges(ecReadOnly, &enum_ranges, comp_range)) ||
        !enum_ranges) {
        return;
    }

    ITfRange *raw_range = nullptr;
    ULONG fetched = 0;
    while (enum_ranges->Next(1, &raw_range, &fetched) == S_OK && fetched == 1) {
        CComPtr<ITfRange> sub_range;
        sub_range.Attach(raw_range);
        raw_range = nullptr;

        CComPtr<ITfRangeACP> range_acp;
        LONG r_start = 0, r_len = 0;
        if (FAILED(sub_range->QueryInterface(IID_PPV_ARGS(&range_acp))) ||
            !range_acp || FAILED(range_acp->GetExtent(&r_start, &r_len)) ||
            r_len <= 0) {
            continue;
        }

        LONG s = std::clamp<LONG>(r_start, comp_start_, comp_end_);
        LONG e = std::clamp<LONG>(r_start + r_len, s, comp_end_);
        if (e <= s)
            continue;

        VARIANT var;
        VariantInit(&var);
        if (SUCCEEDED(prop->GetValue(ecReadOnly, sub_range, &var)) &&
            var.vt == VT_I4 && var.lVal != TF_INVALID_GUIDATOM) {
            GUID attr_guid{};
            if (SUCCEEDED(owner_->category_mgr->GetGUID(
                    static_cast<TfGuidAtom>(var.lVal), &attr_guid))) {
                CComPtr<ITfDisplayAttributeInfo> attr_info;
                CLSID clsid{};
                if (SUCCEEDED(owner_->display_attr_mgr->GetDisplayAttributeInfo(
                        attr_guid, &attr_info, &clsid)) &&
                    attr_info) {
                    TF_DISPLAYATTRIBUTE da{};
                    if (SUCCEEDED(attr_info->GetAttributeInfo(&da))) {
                        uint8_t style = MapDisplayAttributeStyle(da);
                        std::fill(comp_styles_.begin() + (s - comp_start_),
                                  comp_styles_.begin() + (e - comp_start_),
                                  style);
                    }
                }
            }
        }
        VariantClear(&var);
    }
}

void TsfTextStore::EmitPreedit() {
    if (!owner_ || !owner_->callbacks.on_preedit_changed)
        return;
    LONG doc_len = static_cast<LONG>(doc_text_.size());
    LONG s = std::clamp<LONG>(comp_start_, 0, doc_len);
    LONG e = std::clamp<LONG>(comp_end_, s, doc_len);
    if (comp_active_ && e > s) {
        LONG len = e - s;
        LONG caret = std::clamp<LONG>(sel_start_, s, e) - s;
        const uint8_t *styles =
            (comp_styles_.size() == static_cast<size_t>(len))
                ? comp_styles_.data()
                : nullptr;
        owner_->callbacks.on_preedit_changed(
            owner_->host_ctx, doc_text_.data() + s, styles,
            static_cast<int>(len), static_cast<int>(caret));
    } else {
        owner_->callbacks.on_preedit_changed(owner_->host_ctx, nullptr, nullptr,
                                             0, 0);
    }
}

void TsfTextStore::RefreshContext(bool notify_sink) {
    if (!owner_ || !owner_->callbacks.get_context_text)
        return;
    if (comp_active_)
        return;
    if (IsInLock()) {
        pending_context_refresh_ = true;
        return;
    }

    wchar_t before_buf[kContextBufMax];
    wchar_t after_buf[kContextBufMax];
    int before_len = 0;
    int after_len = 0;
    owner_->callbacks.get_context_text(owner_->host_ctx, before_buf,
                                       kContextBufMax, &before_len, after_buf,
                                       kContextBufMax, &after_len);

    std::wstring next_text;
    next_text.reserve(static_cast<size_t>(before_len + after_len));
    if (before_len > 0)
        next_text.append(before_buf, static_cast<size_t>(before_len));
    if (after_len > 0)
        next_text.append(after_buf, static_cast<size_t>(after_len));

    LONG next_sel = static_cast<LONG>(before_len);
    bool text_changed = (next_text != doc_text_);
    bool sel_changed = (next_sel != sel_start_ || next_sel != sel_end_);
    if (!text_changed && !sel_changed)
        return;

    LONG old_len = static_cast<LONG>(doc_text_.size());
    doc_text_ = std::move(next_text);
    sel_start_ = next_sel;
    sel_end_ = next_sel;
    sel_ase_ = TS_AE_END;
    comp_start_ = next_sel;
    comp_end_ = next_sel;

    if (notify_sink && text_sink_) {
        if (text_changed && (text_sink_mask_ & TS_AS_TEXT_CHANGE)) {
            TS_TEXTCHANGE chg{};
            chg.acpStart = 0;
            chg.acpOldEnd = old_len;
            chg.acpNewEnd = static_cast<LONG>(doc_text_.size());
            text_sink_->OnTextChange(0, &chg);
        }
        if ((text_changed || sel_changed) &&
            (text_sink_mask_ & TS_AS_SEL_CHANGE)) {
            text_sink_->OnSelectionChange();
        }
    }
}

void TsfTextStore::NotifyLayoutChanged() {
    if (!owner_ || !text_sink_ || !(text_sink_mask_ & TS_AS_LAYOUT_CHANGE))
        return;
    if (IsInLock()) {
        pending_layout_change_ = true;
        return;
    }
    text_sink_->OnLayoutChange(TS_LC_CHANGE, kViewCookie);
}

void TsfTextStore::FlushDeferredNotifications() {
    if (IsInLock())
        return;
    if (pending_context_refresh_) {
        pending_context_refresh_ = false;
        RefreshContext(true);
    }
    if (pending_layout_change_) {
        pending_layout_change_ = false;
        if (text_sink_ && (text_sink_mask_ & TS_AS_LAYOUT_CHANGE))
            text_sink_->OnLayoutChange(TS_LC_CHANGE, kViewCookie);
    }
}

/* ---------- TsfTextStore IUnknown ---------- */

IFACEMETHODIMP TsfTextStore::QueryInterface(REFIID riid, void **ppv) {
    if (!ppv)
        return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextStoreACP)) {
        *ppv = static_cast<ITextStoreACP *>(this);
    } else if (IsEqualIID(riid, IID_ITfContextOwnerCompositionSink)) {
        *ppv = static_cast<ITfContextOwnerCompositionSink *>(this);
    } else if (IsEqualIID(riid, IID_ITfTextEditSink)) {
        *ppv = static_cast<ITfTextEditSink *>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

/* ---------- TsfTextStore ITextStoreACP ---------- */

IFACEMETHODIMP TsfTextStore::AdviseSink(REFIID riid, IUnknown *punk,
                                        DWORD dwMask) {
    if (!IsEqualIID(riid, IID_ITextStoreACPSink))
        return E_INVALIDARG;
    if (!punk)
        return E_INVALIDARG;

    CComPtr<IUnknown> new_unk;
    if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&new_unk))))
        return E_INVALIDARG;

    if (text_sink_) {
        CComPtr<IUnknown> cur_unk;
        text_sink_->QueryInterface(IID_PPV_ARGS(&cur_unk));
        if (cur_unk == new_unk) {
            text_sink_mask_ = dwMask;
            return S_OK;
        }
        return CONNECT_E_ADVISELIMIT;
    }

    if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&text_sink_))) || !text_sink_)
        return E_INVALIDARG;

    text_sink_mask_ = dwMask;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::UnadviseSink(IUnknown *punk) {
    if (!punk || !text_sink_)
        return CONNECT_E_NOCONNECTION;
    CComPtr<IUnknown> new_unk;
    CComPtr<IUnknown> cur_unk;
    if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&new_unk))) ||
        FAILED(text_sink_->QueryInterface(IID_PPV_ARGS(&cur_unk))) ||
        new_unk != cur_unk) {
        return CONNECT_E_NOCONNECTION;
    }
    text_sink_.Release();
    text_sink_mask_ = 0;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::RequestLock(DWORD dwLockFlags,
                                         HRESULT *phrSession) {
    if (!phrSession)
        return E_INVALIDARG;
    *phrSession = E_FAIL;
    if (!text_sink_)
        return E_FAIL;

    DWORD req = dwLockFlags & TS_LF_READWRITE;
    if (req == 0)
        return E_INVALIDARG;

    if (IsInLock()) {
        if (dwLockFlags & TS_LF_SYNC) {
            *phrSession = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        pending_lock_flags_ |= req;
        *phrSession = TS_S_ASYNC;
        return S_OK;
    }

    CComPtr<ITextStoreACPSink> sink_guard = text_sink_;
    CComPtr<ITextStoreACP> self_guard = this;

    lock_type_ = req;
    *phrSession = sink_guard->OnLockGranted(lock_type_);
    lock_type_ = 0;

    while (pending_lock_flags_ != 0 && text_sink_) {
        DWORD next_req = pending_lock_flags_;
        pending_lock_flags_ = 0;
        lock_type_ = next_req;
        text_sink_->OnLockGranted(lock_type_);
        lock_type_ = 0;
    }

    FlushDeferredNotifications();
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetStatus(TS_STATUS *pdcs) {
    if (!pdcs)
        return E_INVALIDARG;
    pdcs->dwDynamicFlags = 0;
    pdcs->dwStaticFlags = TS_SS_NOHIDDENTEXT;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::QueryInsert(LONG acpTestStart, LONG acpTestEnd,
                                         ULONG /*cch*/, LONG *pacpResultStart,
                                         LONG *pacpResultEnd) {
    if (!pacpResultStart || !pacpResultEnd)
        return E_INVALIDARG;
    LONG doc_len = static_cast<LONG>(doc_text_.size());
    LONG s = std::clamp<LONG>(acpTestStart, 0, doc_len);
    LONG e = std::clamp<LONG>(acpTestEnd, s, doc_len);
    *pacpResultStart = s;
    *pacpResultEnd = e;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetSelection(ULONG ulIndex, ULONG ulCount,
                                          TS_SELECTION_ACP *pSelection,
                                          ULONG *pcFetched) {
    if (!pcFetched)
        return E_INVALIDARG;
    *pcFetched = 0;
    if (!IsLocked(TS_LF_READ))
        return TS_E_NOLOCK;
    if (ulCount == 0)
        return S_OK;
    if (!pSelection)
        return E_INVALIDARG;
    if (ulIndex != TF_DEFAULT_SELECTION && ulIndex != 0)
        return TS_E_NOSELECTION;

    pSelection[0].acpStart = sel_start_;
    pSelection[0].acpEnd = sel_end_;
    pSelection[0].style.ase = sel_ase_;
    pSelection[0].style.fInterimChar = FALSE;
    *pcFetched = 1;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::SetSelection(ULONG ulCount,
                                          const TS_SELECTION_ACP *pSelection) {
    if (!IsLocked(TS_LF_READWRITE))
        return TS_E_NOLOCK;
    if (ulCount != 1 || !pSelection)
        return E_INVALIDARG;

    LONG doc_len = static_cast<LONG>(doc_text_.size());
    LONG s = pSelection[0].acpStart;
    LONG e = pSelection[0].acpEnd;
    if (s < 0 || e < s || e > doc_len)
        return TS_E_INVALIDPOS;

    sel_start_ = s;
    sel_end_ = e;
    sel_ase_ = pSelection[0].style.ase;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetText(LONG acpStart, LONG acpEnd, WCHAR *pchPlain,
                                     ULONG cchPlainReq, ULONG *pcchPlainRet,
                                     TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq,
                                     ULONG *pcRunInfoRet, LONG *pacpNext) {
    if (!IsLocked(TS_LF_READ))
        return TS_E_NOLOCK;
    if (!pcchPlainRet)
        return E_INVALIDARG;

    LONG doc_len = static_cast<LONG>(doc_text_.size());
    if (acpStart < 0 || acpStart > doc_len)
        return TS_E_INVALIDPOS;
    if (acpEnd == -1)
        acpEnd = doc_len;
    if (acpEnd < acpStart || acpEnd > doc_len)
        return TS_E_INVALIDPOS;

    ULONG avail = static_cast<ULONG>(acpEnd - acpStart);
    ULONG copied = 0;
    if (pchPlain && cchPlainReq > 0) {
        copied = std::min(cchPlainReq, avail);
        if (copied > 0) {
            std::memcpy(pchPlain, doc_text_.data() + acpStart,
                        copied * sizeof(WCHAR));
        }
        *pcchPlainRet = copied;
    } else {
        *pcchPlainRet = 0;
        copied = avail;
    }

    if (pcRunInfoRet) {
        if (prgRunInfo && cRunInfoReq > 0 && copied > 0) {
            prgRunInfo[0].uCount = copied;
            prgRunInfo[0].type = TS_RT_PLAIN;
            *pcRunInfoRet = 1;
        } else {
            *pcRunInfoRet = 0;
        }
    }

    if (pacpNext)
        *pacpNext = acpStart + static_cast<LONG>(copied);

    return S_OK;
}

IFACEMETHODIMP TsfTextStore::SetText(DWORD /*dwFlags*/, LONG acpStart,
                                     LONG acpEnd, const WCHAR *pchText,
                                     ULONG cch, TS_TEXTCHANGE *pChange) {
    if (!IsLocked(TS_LF_READWRITE))
        return TS_E_NOLOCK;

    LONG doc_len = static_cast<LONG>(doc_text_.size());
    if (acpStart < 0 || acpEnd < acpStart || acpEnd > doc_len)
        return TS_E_INVALIDPOS;
    if (cch > 0 && !pchText)
        return E_INVALIDARG;

    doc_text_.replace(static_cast<size_t>(acpStart),
                      static_cast<size_t>(acpEnd - acpStart),
                      pchText ? pchText : L"", static_cast<size_t>(cch));

    LONG new_end = acpStart + static_cast<LONG>(cch);
    LONG delta = static_cast<LONG>(cch) - (acpEnd - acpStart);

    sel_start_ = new_end;
    sel_end_ = new_end;
    sel_ase_ = TS_AE_END;

    if (comp_active_) {
        if (acpEnd <= comp_start_) {
            comp_start_ += delta;
            comp_end_ += delta;
        } else if (acpStart < comp_end_) {
            comp_start_ = std::min(comp_start_, acpStart);
            comp_end_ = std::max(comp_end_ + delta, new_end);
        }
    } else if (cch > 0 && pchText) {
        pending_direct_text_.append(pchText, cch);
    }

    if (pChange) {
        pChange->acpStart = acpStart;
        pChange->acpOldEnd = acpEnd;
        pChange->acpNewEnd = new_end;
    }
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::InsertTextAtSelection(
    DWORD dwFlags, const WCHAR *pchText, ULONG cch, LONG *pacpStart,
    LONG *pacpEnd, TS_TEXTCHANGE *pChange) {
    if (!IsLocked(TS_LF_READWRITE))
        return TS_E_NOLOCK;

    if (dwFlags & TF_IAS_QUERYONLY) {
        if (pacpStart)
            *pacpStart = sel_start_;
        if (pacpEnd)
            *pacpEnd = sel_end_;
        if (pChange) {
            pChange->acpStart = sel_start_;
            pChange->acpOldEnd = sel_end_;
            pChange->acpNewEnd = sel_end_;
        }
        return S_OK;
    }

    TS_TEXTCHANGE chg{};
    HRESULT hr = SetText(0, sel_start_, sel_end_, pchText, cch, &chg);
    if (FAILED(hr))
        return hr;

    if (!(dwFlags & TF_IAS_NOQUERY)) {
        if (pacpStart)
            *pacpStart = chg.acpStart;
        if (pacpEnd)
            *pacpEnd = chg.acpNewEnd;
    }
    if (pChange)
        *pChange = chg;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetEndACP(LONG *pacp) {
    if (!pacp)
        return E_INVALIDARG;
    if (!IsLocked(TS_LF_READ))
        return TS_E_NOLOCK;
    *pacp = static_cast<LONG>(doc_text_.size());
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetActiveView(TsViewCookie *pvcView) {
    if (!pvcView)
        return E_INVALIDARG;
    *pvcView = kViewCookie;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetACPFromPoint(TsViewCookie vcView,
                                             const POINT * /*ptScreen*/,
                                             DWORD /*dwFlags*/, LONG *pacp) {
    if (vcView != kViewCookie || !pacp)
        return E_INVALIDARG;
    if (!IsLocked(TS_LF_READ))
        return TS_E_NOLOCK;
    return TS_E_NOLAYOUT;
}

IFACEMETHODIMP TsfTextStore::GetTextExt(TsViewCookie vcView, LONG acpStart,
                                        LONG acpEnd, RECT *prc,
                                        BOOL *pfClipped) {
    if (vcView != kViewCookie || !prc || !pfClipped)
        return E_INVALIDARG;
    if (!IsLocked(TS_LF_READ))
        return TS_E_NOLOCK;

    LONG doc_len = static_cast<LONG>(doc_text_.size());
    if (acpStart < 0 || acpEnd < acpStart || acpEnd > doc_len)
        return TS_E_INVALIDPOS;

    if (!owner_ || !owner_->callbacks.get_text_ext)
        return TS_E_NOLAYOUT;

    LONG base = comp_active_ ? comp_start_ : sel_start_;
    LONG comp_len = comp_active_ ? std::max<LONG>(0, comp_end_ - comp_start_) : 0;
    int preedit_cols = comp_active_ ? MeasureCells(base, base + comp_len) : 0;

    LONG s = std::clamp<LONG>(acpStart, base, base + comp_len);
    LONG e = std::clamp<LONG>(acpEnd, s, base + comp_len);
    int start_col = comp_active_ ? MeasureCells(base, s) : 0;
    int end_col = comp_active_ ? MeasureCells(base, e) : 0;

    if (!owner_->callbacks.get_text_ext(owner_->host_ctx, preedit_cols,
                                        start_col, end_col, prc)) {
        return TS_E_NOLAYOUT;
    }

    *pfClipped = FALSE;
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetScreenExt(TsViewCookie vcView, RECT *prc) {
    if (vcView != kViewCookie || !prc)
        return E_INVALIDARG;
    if (!owner_ || !owner_->hwnd || !IsWindow(owner_->hwnd))
        return E_FAIL;
    if (!GetClientRect(owner_->hwnd, prc))
        return E_FAIL;
    MapWindowPoints(owner_->hwnd, HWND_DESKTOP,
                    reinterpret_cast<POINT *>(prc), 2);
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::GetWnd(TsViewCookie vcView, HWND *phwnd) {
    if (vcView != kViewCookie || !phwnd)
        return E_INVALIDARG;
    *phwnd = owner_ ? owner_->hwnd : nullptr;
    return S_OK;
}

/* ---------- ITfContextOwnerCompositionSink ---------- */

IFACEMETHODIMP TsfTextStore::OnStartComposition(
    ITfCompositionView *pComposition, BOOL *pfOk) {
    if (!pfOk)
        return E_INVALIDARG;
    if (active_comp_) {
        *pfOk = FALSE;
        return S_OK;
    }
    *pfOk = TRUE;
    active_comp_ = pComposition;
    comp_active_ = true;
    pending_direct_text_.clear();

    LONG c_start = sel_start_;
    LONG c_len = 0;
    if (GetRangeExtent(pComposition, &c_start, &c_len)) {
        LONG doc_len = static_cast<LONG>(doc_text_.size());
        comp_start_ = std::clamp<LONG>(c_start, 0, doc_len);
        comp_end_ = std::clamp<LONG>(c_start + c_len, comp_start_, doc_len);
    } else {
        comp_start_ = sel_start_;
        comp_end_ = sel_end_;
    }
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::OnUpdateComposition(
    ITfCompositionView * /*pComposition*/, ITfRange * /*pRangeNew*/) {
    return S_OK;
}

IFACEMETHODIMP TsfTextStore::OnEndComposition(
    ITfCompositionView *pComposition) {
    if (!owner_) {
        comp_active_ = false;
        active_comp_.Release();
        return S_OK;
    }

    LONG end_start = comp_start_;
    LONG end_len = 0;
    if (GetRangeExtent(pComposition, &end_start, &end_len)) {
        LONG doc_len = static_cast<LONG>(doc_text_.size());
        end_start = std::clamp<LONG>(end_start, 0, doc_len);
        end_len = std::clamp<LONG>(end_len, 0, doc_len - end_start);
        if (comp_active_ && end_start > comp_start_)
            CommitTextSlice(comp_start_, end_start);
        if (end_len > 0)
            CommitTextSlice(end_start, end_start + end_len);
    } else if (comp_active_ && comp_end_ > comp_start_) {
        CommitTextSlice(comp_start_, comp_end_);
        end_start = comp_end_;
        end_len = 0;
    }

    comp_active_ = false;
    comp_start_ = end_start + end_len;
    comp_end_ = comp_start_;
    comp_styles_.clear();
    active_comp_.Release();
    pending_direct_text_.clear();
    EmitPreedit();
    return S_OK;
}

/* ---------- ITfTextEditSink ---------- */

IFACEMETHODIMP TsfTextStore::OnEndEdit(ITfContext *pic,
                                       TfEditCookie ecReadOnly,
                                       ITfEditRecord * /*pEditRecord*/) {
    if (!owner_)
        return S_OK;

    if (comp_active_ && active_comp_) {
        LONG new_start = comp_start_;
        LONG new_len = 0;
        if (GetRangeExtent(active_comp_, &new_start, &new_len)) {
            LONG doc_len = static_cast<LONG>(doc_text_.size());
            new_start = std::clamp<LONG>(new_start, 0, doc_len);
            new_len = std::clamp<LONG>(new_len, 0, doc_len - new_start);

            if (new_start > comp_start_) {
                CommitTextSlice(comp_start_, new_start);
            }
            comp_start_ = new_start;
            comp_end_ = new_start + new_len;
        }
        UpdatePreeditStyles(pic, ecReadOnly);
        EmitPreedit();
    } else if (!pending_direct_text_.empty()) {
        std::wstring committed = std::move(pending_direct_text_);
        pending_direct_text_.clear();
        if (owner_->callbacks.on_commit_text) {
            owner_->callbacks.on_commit_text(
                owner_->host_ctx, committed.data(),
                static_cast<int>(committed.size()));
        }
    }

    return S_OK;
}

} /* namespace */

extern "C" {

TsfState *tsf_init(HWND hwnd, void *host_ctx, const TsfHostCallbacks *callbacks) {
    if (!hwnd || !IsWindow(hwnd))
        return nullptr;

    TsfState *state = new (std::nothrow) TsfState();
    if (!state)
        return nullptr;

    state->hwnd = hwnd;
    state->host_ctx = host_ctx;
    if (callbacks)
        state->callbacks = *callbacks;

    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&state->thread_mgr))) ||
        !state->thread_mgr) {
        tsf_deinit(state);
        return nullptr;
    }

    if (FAILED(state->thread_mgr->Activate(&state->client_id)) ||
        state->client_id == TF_CLIENTID_NULL) {
        tsf_deinit(state);
        return nullptr;
    }
    state->active = true;

    state->sink = new (std::nothrow) TsfSink(state);
    state->store = new (std::nothrow) TsfTextStore(state);
    if (!state->sink || !state->store) {
        tsf_deinit(state);
        return nullptr;
    }

    if (FAILED(state->thread_mgr->CreateDocumentMgr(&state->doc_mgr)) ||
        !state->doc_mgr) {
        tsf_deinit(state);
        return nullptr;
    }

    if (FAILED(state->doc_mgr->CreateContext(
            state->client_id, 0, static_cast<ITextStoreACP *>(state->store),
            &state->context, &state->edit_cookie)) ||
        !state->context) {
        tsf_deinit(state);
        return nullptr;
    }

    if (FAILED(state->doc_mgr->Push(state->context))) {
        tsf_deinit(state);
        return nullptr;
    }

    AdviseSinkHelper<ITfTextEditSink>(state->context, state->store,
                                      &state->edit_sink_cookie);

    if (FAILED(state->thread_mgr->AssociateFocus(hwnd, state->doc_mgr,
                                                 &state->prev_doc_mgr))) {
        tsf_deinit(state);
        return nullptr;
    }

    state->thread_mgr->QueryInterface(IID_PPV_ARGS(&state->keystroke_mgr));
    CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&state->category_mgr));
    CoCreateInstance(CLSID_TF_DisplayAttributeMgr, nullptr,
                     CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&state->display_attr_mgr));
    state->composition_active = true;

    AdviseSinkHelper<ITfThreadMgrEventSink>(state->thread_mgr, state->sink,
                                            &state->thread_mgr_sink_cookie);

    CComPtr<ITfCompartmentMgr> comp_mgr;
    if (SUCCEEDED(state->thread_mgr->QueryInterface(IID_PPV_ARGS(&comp_mgr))) &&
        comp_mgr) {
        if (SUCCEEDED(comp_mgr->GetCompartment(
                GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &state->open_close_comp)) &&
            state->open_close_comp) {
            AdviseSinkHelper<ITfCompartmentEventSink>(
                state->open_close_comp, state->sink,
                &state->open_close_sink_cookie);
        }
    }

    state->store->RefreshContext(false);
    if (GetFocus() == hwnd) {
        state->thread_mgr->SetFocus(state->doc_mgr);
    }
    SyncImeOpenState(state, true);
    return state;
}

void tsf_deinit(TsfState *tsf) {
    if (!tsf)
        return;

    if (tsf->sink)
        tsf->sink->Detach();
    if (tsf->store)
        tsf->store->Detach();

    if (tsf->host_ctx && tsf->callbacks.on_preedit_changed) {
        tsf->callbacks.on_preedit_changed(tsf->host_ctx, nullptr, nullptr, 0, 0);
    }

    UnadviseSinkHelper(tsf->open_close_comp, &tsf->open_close_sink_cookie);
    UnadviseSinkHelper(tsf->thread_mgr, &tsf->thread_mgr_sink_cookie);
    UnadviseSinkHelper(tsf->context, &tsf->edit_sink_cookie);

    if (tsf->active && tsf->thread_mgr && tsf->hwnd && IsWindow(tsf->hwnd)) {
        CComPtr<ITfDocumentMgr> unused;
        tsf->thread_mgr->AssociateFocus(tsf->hwnd, tsf->prev_doc_mgr, &unused);
    }

    if (tsf->doc_mgr)
        tsf->doc_mgr->Pop(TF_POPF_ALL);

    if (tsf->active && tsf->thread_mgr) {
        tsf->thread_mgr->Deactivate();
        tsf->active = false;
    }
    tsf->composition_active = false;

    if (tsf->sink) {
        tsf->sink->Release();
        tsf->sink = nullptr;
    }
    if (tsf->store) {
        tsf->store->Release();
        tsf->store = nullptr;
    }

    tsf->display_attr_mgr.Release();
    tsf->category_mgr.Release();
    tsf->keystroke_mgr.Release();
    tsf->context.Release();
    tsf->prev_doc_mgr.Release();
    tsf->doc_mgr.Release();
    tsf->open_close_comp.Release();
    tsf->thread_mgr.Release();
    delete tsf;
}

bool tsf_is_active(const TsfState *tsf) {
    return tsf && tsf->active;
}

bool tsf_is_composition_active(const TsfState *tsf) {
    return tsf && tsf->active && tsf->composition_active;
}

bool tsf_is_ime_open(const TsfState *tsf) {
    if (!tsf || !tsf->active)
        return false;
    return ReadCompartmentBool(tsf->open_close_comp, tsf->ime_open);
}

void tsf_on_set_focus(TsfState *tsf) {
    if (!tsf || !tsf->active)
        return;
    if (tsf->store && !tsf->store->IsCompActive())
        tsf->store->RefreshContext(true);
    SyncImeOpenState(tsf, false);
}

void tsf_on_kill_focus(TsfState * /*tsf*/) {
}

void tsf_on_inputlang_change(TsfState *tsf) {
    SyncImeOpenState(tsf, true);
}

void tsf_notify_cursor_moved(TsfState *tsf) {
    if (!tsf || !tsf->active || !tsf->store)
        return;
    if (!tsf->store->IsCompActive())
        tsf->store->RefreshContext(true);
    tsf->store->NotifyLayoutChanged();
}

bool tsf_filter_key(TsfState *tsf, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!tsf || !tsf->active || !tsf->composition_active || !tsf->keystroke_mgr)
        return false;

    BOOL eaten = FALSE;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (SUCCEEDED(tsf->keystroke_mgr->TestKeyDown(wParam, lParam, &eaten)) &&
            eaten) {
            eaten = FALSE;
            tsf->keystroke_mgr->KeyDown(wParam, lParam, &eaten);
            return eaten != FALSE;
        }
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if (SUCCEEDED(tsf->keystroke_mgr->TestKeyUp(wParam, lParam, &eaten)) &&
            eaten) {
            eaten = FALSE;
            tsf->keystroke_mgr->KeyUp(wParam, lParam, &eaten);
            return eaten != FALSE;
        }
    }
    return false;
}

} /* extern "C" */
