#pragma once

//
// overlay_window.h — Fullscreen screenshot selection overlay.
//
// Capture overlay: rectangular / fullscreen / freeform / color picker / OCR.
// Editor overlay:  shown only after a snip is confirmed.
//

#include <windows.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <functional>
#include "core/frame_buffer.h"
#include "core/selection.h"
#include "../capture/dxgi_capture.h"
#include "annotation.h"
#include "tools/rect_tool.h"
#include "tools/arrow_tool.h"
#include "tools/text_tool.h"
#include "tools/blur_tool.h"
#include "ocr_extract.h"
#include "../qr/qr_extract.h"
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

namespace snippy {

using ConfirmCallback = std::function<void(RECT selectedRect)>;
using CancelCallback  = std::function<void()>;

class OverlayWindow {
public:
    bool Initialize(HINSTANCE hInstance) noexcept;
    void Show(const FrameBuffer& frameBuffer, const MonitorInfo& monitor) noexcept;
    void Hide() noexcept;
    void Shutdown() noexcept;

    ConfirmCallback OnConfirm;
    CancelCallback  OnCancel;
    std::function<void()> OnRequestNewSnip;

    void ReloadSettings() noexcept;

    [[nodiscard]] HWND Hwnd() const noexcept { return m_hwnd; }
    [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void OnLButtonDown(POINT pt) noexcept;
    void OnMouseMove(POINT pt) noexcept;
    void OnLButtonUp(POINT pt) noexcept;
    void OnKeyDown(WPARAM vk) noexcept;
    void OnChar(WPARAM ch) noexcept;

    bool CreateSwapChain() noexcept;
    bool CreateRenderTarget() noexcept;
    bool CreateResources() noexcept;
    void EnsureIntermediateTexture(UINT width, UINT height) noexcept;

    void RenderFrame() noexcept;
    void DrawDimOverlay(ID2D1DeviceContext* dc) noexcept;
    void DrawSelection(ID2D1DeviceContext* dc) noexcept;
    void DrawSizeLabel(ID2D1DeviceContext* dc, const RECT& sel) noexcept;
    void DrawLasso(ID2D1DeviceContext* dc) noexcept;
    void DrawColorPickerHud(ID2D1DeviceContext* dc, POINT local) noexcept;
    void RenderFloatingToolbar(ID2D1DeviceContext* dc) noexcept;
    void DrawToolbarIcon(ID2D1DeviceContext* dc, int id, D2D1_RECT_F rect,
                         bool isActive, bool isHovered) noexcept;
    void DrawFluentGlyph(ID2D1DeviceContext* dc, WCHAR glyph, D2D1_RECT_F rect,
                         ID2D1Brush* foreground,
                         IDWriteTextFormat* format = nullptr) noexcept;
    void RenderAnnotations(ID2D1DeviceContext* dc) noexcept;
    void RenderArrowAnnotation(ID2D1DeviceContext* dc, const ArrowAnnotation& aa) noexcept;
    void RenderBlurAnnotation(ID2D1DeviceContext* dc, const BlurAnnotation& ba) noexcept;
    void RenderFreeDrawAnnotation(ID2D1DeviceContext* dc, const FreeDrawAnnotation& fd) noexcept;
    void RenderCounterAnnotation(ID2D1DeviceContext* dc, const CounterAnnotation& ca) noexcept;
    int  GetNextCounterNumber() const noexcept;
    void RenderToolbarPopup(ID2D1DeviceContext* dc) noexcept;
    void RenderPreview(ID2D1DeviceContext* dc) noexcept;

    void LoadToolbarSettings() noexcept;
    void BuildToolbar() noexcept;
    bool PointInToolbar(POINT localPt) const noexcept;
    int  HitTestToolbar(POINT pt) const noexcept;
    bool PointInToolbarPopup(POINT localPt) const noexcept;
    bool HitTestToolbarPopup(POINT localPt) const noexcept;
    bool HandleToolbarPopupClick(POINT localPt) noexcept;
    void OnToolbarClick(int btn) noexcept;
    void PushUndoState() noexcept;
    void Undo() noexcept;
    void Redo() noexcept;
    void ClearAnnotations() noexcept;
    void CopyToClipboard() noexcept;
    void SaveToFile() noexcept;
    bool RenderSnipImage(std::vector<BYTE>& outBgra, UINT& outW, UINT& outH) noexcept;
    void ApplyFullscreenSelection() noexcept;
    void CommitLassoSelection() noexcept;
    void PickColorAt(POINT screenPt) noexcept;
    void FinishTextExtract() noexcept;
    void StartTextExtract() noexcept;
    void RenderOcrOverlay(ID2D1DeviceContext* dc) noexcept;
    int  GetClosestOcrCharIndex(D2D1_POINT_2F pt) const noexcept;
    std::wstring GetSelectedOcrText() const noexcept;
    void OnOcrComplete(const OcrInlineResult& res) noexcept;
    void StartAutoQrScan() noexcept;
    void RenderQrHighlight(ID2D1DeviceContext* dc) noexcept;
    void OnQrComplete(uint32_t seq, const QrResult& res) noexcept;
    bool SamplePixel(POINT screenPt, uint8_t& r, uint8_t& g, uint8_t& b) const noexcept;
    void EraseAt(D2D1_POINT_2F pt) noexcept;
    bool AnnotationHit(const Annotation& ann, D2D1_POINT_2F pt) const noexcept;
    int HitTestSelectionHandle(POINT screenPt) const noexcept;
    void ResizeSelection(POINT screenPt) noexcept;
    RECT BufferRect(const RECT& screenSel) const noexcept;
    void UpdateCursor(POINT localPt) noexcept;
    [[nodiscard]] bool IsEditorOpen() const noexcept;
    [[nodiscard]] bool IsPointInFreeform(D2D1_POINT_2F pt) const noexcept;

    D2D1_RECT_F GetAnnotationBounds(const Annotation& ann) const noexcept;
    int  HitTestSelectedAnnotationHandle(D2D1_POINT_2F pt) const noexcept;
    void ResizeOrMoveAnnotation(D2D1_POINT_2F cpt) noexcept;
    void DrawSelectedAnnotationOutline(ID2D1DeviceContext* dc) noexcept;
    void PopulateSystemFonts() noexcept;

    HWND      m_hwnd      = nullptr;
    HINSTANCE m_hInstance = nullptr;

    ComPtr<IDCompositionDevice>  m_dcompDevice;
    ComPtr<IDCompositionTarget>  m_dcompTarget;
    ComPtr<IDCompositionVisual>  m_dcompVisual;
    ComPtr<IDXGISwapChain1>      m_swapChain;
    ComPtr<ID2D1Bitmap1>         m_d2dTarget;
    ComPtr<ID2D1Bitmap>          m_desktopBitmap;
    ComPtr<ID3D11Texture2D>      m_blurIntermediateTex;
    ComPtr<ID2D1Bitmap1>         m_blurIntermediateBitmap;
    ID3D11Texture2D*             m_activeRenderTexture = nullptr;

    ComPtr<ID2D1SolidColorBrush> m_dimBrush;
    ComPtr<ID2D1SolidColorBrush> m_selBorderBrush;
    ComPtr<ID2D1SolidColorBrush> m_handleBrush;
    ComPtr<ID2D1SolidColorBrush> m_labelBgBrush;
    ComPtr<ID2D1SolidColorBrush> m_labelFgBrush;
    ComPtr<ID2D1SolidColorBrush> m_guideBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarBgBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarBorderBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarShadowBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarIconBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarCloseBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarCloseHoverBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarCopyBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarActiveBrush;
    ComPtr<ID2D1SolidColorBrush> m_toolbarHoverBrush;
    ComPtr<ID2D1SolidColorBrush> m_separatorBrush;
    ComPtr<ID2D1SolidColorBrush> m_strokeBrush;
    ComPtr<ID2D1SolidColorBrush> m_highlightBrush;
    ComPtr<ID2D1SolidColorBrush> m_previewBrush;
    ComPtr<ID2D1SolidColorBrush> m_swatchBrush;
    ComPtr<ID2D1StrokeStyle>     m_roundStroke;
    ComPtr<ID2D1StrokeStyle>     m_dashStroke;

    std::vector<Annotation>              m_annotations;
    std::vector<std::vector<Annotation>> m_undoStack;
    std::vector<std::vector<Annotation>> m_redoStack;
    static constexpr size_t kMaxUndo = 50;

    CaptureMode m_captureMode = CaptureMode::Region;
    ToolMode    m_toolMode    = ToolMode::Select;
    ColorF      m_strokeColor{ 1.f, 0.23f, 0.23f, 1.f };
    float       m_strokeWidth = 3.0f;
    float       m_shapeStrokeWidth = 3.0f;
    float       m_fontSize    = 18.0f;
    std::wstring m_fontFamily = L"Segoe UI";
    float       m_blurRadius  = 10.0f;
    BlurStyle   m_blurStyle   = BlurStyle::Gaussian;
    bool        m_toolbarDark = true;
    bool        m_toolbarAtTop = true;
    float       m_dimmingOpacity = 0.5f;
    float       m_selectionBorderThickness = 2.0f;
    D2D1_COLOR_F m_toolbarAccent{ 0.f, 0.471f, 0.831f, 1.f };
    std::vector<std::wstring> m_hiddenTools;
    std::vector<std::wstring> m_toolbarOrder;

    RectToolState  m_rectState{};
    ArrowToolState m_arrowState{};
    TextToolState  m_textState{};
    BlurToolState  m_blurState{};
    struct FreeDrawState {
        std::vector<D2D1_POINT_2F> points;
        bool active = false;
    } m_freeDrawState{};
    std::vector<D2D1_POINT_2F> m_lassoPoints;
    bool m_lassoDrawing = false;
    bool m_dragging = false;
    int m_resizeHandle = -1;
    RECT m_resizeStartRect = {};
    POINT m_resizeStartPoint = {};
    bool m_erasing  = false;
    bool m_eraseUndoPushed = false;
    struct CounterState {
        D2D1_POINT_2F start{};
        D2D1_POINT_2F current{};
        int           step = 1;
        bool          active = false;
    } m_counterState{};

    // Move tool annotation interaction
    int           m_selectedAnnotationIndex = -1;
    int           m_annotationResizeHandle  = -1; // -1 = none, 0..7 = bbox handles, 8 = move body
    D2D1_POINT_2F m_dragStartPoint{};
    Annotation    m_dragStartAnnotation{};
    D2D1_RECT_F   m_annotationStartBounds{};
    bool          m_annotationDragUndoPushed = false;

    // System fonts dropdown
    std::vector<std::wstring> m_systemFonts;
    bool  m_fontDropdownOpen = false;
    int   m_fontScrollIndex  = 0;

    enum class ToolbarButton {
        Move, Shapes, AnnotationColor,
        Region, Fullscreen, Freeform, ColorPicker, TextExtract,
        Pen, Highlighter, Eraser, Blur,
        Rect, Ellipse, Arrow, Text, Counter,
        Undo, Redo, Clear, Save, Copy, Close
    };
    enum class ToolbarPopup { None, Pen, Shapes, Blur, Text, Color, CustomColorPicker };
    struct TBButton {
        ToolbarButton id;
        D2D1_RECT_F   rect;
        const WCHAR*  tooltip;
        bool          hasDropdown = false;
    };
    std::vector<TBButton> m_toolbar;
    std::vector<float>    m_separators;
    D2D1_RECT_F           m_toolbarBounds{};
    D2D1_RECT_F           m_popupBounds{};
    ToolbarPopup          m_toolbarPopup = ToolbarPopup::None;
    bool                  m_fontSizeDragging = false;
    bool                  m_penSizeDragging = false;
    bool                  m_colorSpectrumDragging = false;
    bool                  m_colorHueDragging = false;
    float                 m_customPickerHue = 0.0f;
    float                 m_customPickerSat = 1.0f;
    float                 m_customPickerVal = 1.0f;
    D2D1_COLOR_F          m_customPickerColor = { 1.0f, 0.23f, 0.19f, 1.0f };
    D2D1_COLOR_F          m_customPickerBackupColor = { 1.0f, 0.23f, 0.19f, 1.0f };
    bool                  m_draggingColorPicker = false;
    POINT                 m_colorPickerDragStartPt{};
    D2D1_POINT_2F         m_colorPickerPos{};
    bool                  m_colorPickerPosInitialized = false;
    bool                  m_hexInputActive = false;
    std::wstring          m_hexInputText;
    void                  ApplyHexInput(const std::wstring& hex) noexcept;
    static constexpr float kCaptureToolbarH = 38.0f;
    static constexpr float kEditorToolbarH = 40.0f;
    static constexpr float kBtnSize    = 28.0f;
    static constexpr float kCaptureBtnWidth = 34.0f;
    static constexpr float kEditorBtnWidth = 36.0f;
    static constexpr float kBtnGap     = 2.0f;
    static constexpr float kGroupGap   = 8.0f;
    static constexpr float kBarPadX    = 8.0f;
    static constexpr float kBarPadY    = 5.0f;
    static constexpr float kToolbarMargin = 24.0f;
    POINT   m_hoverPt{};
    uint8_t m_hoverR = 0;
    uint8_t m_hoverG = 0;
    uint8_t m_hoverB = 0;

    // OCR extraction state
    bool                     m_ocrActive = false;
    bool                     m_ocrLoading = false;
    OcrInlineResult          m_ocrResult{};
    std::vector<int>         m_selectedOcrCharIndices;
    bool                     m_isOcrSelecting = false;
    POINT                    m_ocrDragStartPos{};
    bool                     m_ocrQuickActionVisible = false;
    D2D1_POINT_2F            m_ocrQuickActionPos{};
    D2D1_RECT_F              m_ocrQuickActionRect{};
    D2D1_RECT_F              m_ocrTopPillRect{};
    int                      m_ocrHoverButton = -1; // 0: Back, 1: Copy all, 2: Close
    bool                     m_ocrQuickActionHover = false;

    // Auto QR code detection state
    bool                     m_qrDetected = false;
    QrResult                 m_qrResult{};
    D2D1_RECT_F              m_qrHighlightRect{};
    D2D1_RECT_F              m_qrBadgeRect{};
    bool                     m_qrHovered = false;
    D2D1_RECT_F              m_qrTooltipRect{};
    D2D1_RECT_F              m_qrOpenBtnRect{};
    D2D1_RECT_F              m_qrCopyBtnRect{};
    int                      m_qrTooltipHoverBtn = 0; // 0: None, 1: Open, 2: Copy
    uint32_t                 m_qrScanSeq = 0;

    SelectionState     m_selection;
    bool               m_isReselecting = false;
    Point              m_reselectPrevAnchor = {};
    Point              m_reselectPrevCurrent = {};
    const FrameBuffer* m_frameBuffer = nullptr;
    RECT  m_monitorRect = {};
    UINT  m_width  = 0;
    UINT  m_height = 0;
    bool  m_visible     = false;
    bool  m_initialized = false;

    static constexpr UINT  kSwapBufferCount = 2;
    static constexpr float kDimAlpha        = 0.55f;
    static constexpr float kSelBorderWidth  = 2.0f;
    static constexpr float kHandleSize      = 8.0f;
    static constexpr int   kLabelPadX       = 8;
    static constexpr int   kLabelPadY       = 4;
};

extern OverlayWindow g_overlay;

} // namespace snippy
