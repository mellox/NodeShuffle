#include "NodeShuffleStatusWidget.h"
#include "NodeShuffleStatus.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"

// ns-status-1 (2026-11-15). Draw calls copied from UrDeadReticle.cpp (MakeLines with explicit thickness, MakeText with
// FCoreStyle::GetDefaultFontStyle, layer stepped per element so text is never under the backing). Fixed width: no font measuring,
// so a line longer than the panel runs past its right edge (a game-only check at the user's UI scale).
int32 UNodeShuffleStatusWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
    int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
        OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

    // Draw nothing while a menu shows the cursor (pause menu, inventory, MAM). Done HERE and not via SetVisibility, so the widget keeps
    // painting and reappears when the menu closes (the UrDead / LaserRifle POC rule).
    const APlayerController* PC = GetOwningPlayer();
    if (PC && PC->bShowMouseCursor) { return Layer; }
    if (Body.IsEmpty()) { return Layer; }

    using namespace NodeShuffleStatus;
    const FVector2D Size = AllottedGeometry.GetLocalSize();
    const float SizeX = static_cast<float>(Size.X), SizeY = static_cast<float>(Size.Y); // explicit: no implicit double -> float narrowing
    const float W = PanelWidthPx;
    const float H = Detail.IsEmpty() ? 64.f : 88.f;
    // Ternaries, not a switch on the constexpr position: a switch whose untaken cases the compiler proves dead risks C4702 here.
    const bool bRight = PanelPosition == EPanelPos::TopRight || PanelPosition == EPanelPos::BottomRight;
    const bool bBottom = PanelPosition == EPanelPos::BottomLeft || PanelPosition == EPanelPos::BottomRight;
    const bool bMiddle = PanelPosition == EPanelPos::MiddleLeft;
    const float X = bRight ? (SizeX - W - PanelMarginPx) : PanelMarginPx;
    const float Y = bMiddle ? (SizeY - H) * 0.5f : (bBottom ? (SizeY - H - PanelMarginPx) : PanelMarginPx);
    const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry();
    auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Col, float T, int32 L, bool bAa)
    {
        TArray<FVector2D> Pts; Pts.Add(A); Pts.Add(B);
        FSlateDrawElement::MakeLines(OutDrawElements, L, PG, Pts, ESlateDrawEffect::None, Col, bAa, T);
    };
    // Backing: horizontal 8 px strokes stacked edge to edge (no antialiasing, so no seams and no doubled alpha where they meet).
    const FLinearColor Back(0.02f, 0.02f, 0.03f, 0.72f);
    for (float Row = 4.f; Row < H; Row += 8.f) { Line(FVector2D(X, Y + Row), FVector2D(X + W, Y + Row), Back, 8.f, Layer + 1, false); }
    const FLinearColor Orange(1.0f, 0.55f, 0.10f); // the chat notices' sender colour (NodeShuffleObserveNotice.cpp)
    Line(FVector2D(X + 2.f, Y), FVector2D(X + 2.f, Y + H), Orange, 4.f, Layer + 2, true);

    FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToOffsetPaintGeometry(FVector2D(X + 14.f, Y + 8.f)),
        Title, FCoreStyle::GetDefaultFontStyle("Bold", 15), ESlateDrawEffect::None, Orange);
    FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToOffsetPaintGeometry(FVector2D(X + 14.f, Y + 36.f)),
        Body, FCoreStyle::GetDefaultFontStyle("Regular", 13), ESlateDrawEffect::None, FLinearColor(0.95f, 0.95f, 0.95f));
    if (!Detail.IsEmpty())
    {
        FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToOffsetPaintGeometry(FVector2D(X + 14.f, Y + 60.f)),
            Detail, FCoreStyle::GetDefaultFontStyle("Regular", 12), ESlateDrawEffect::None, FLinearColor(0.78f, 0.78f, 0.78f));
    }
    return Layer + 3;
}
