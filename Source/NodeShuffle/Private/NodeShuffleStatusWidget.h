// ns-status-1 (2026-11-15): the HOST's first-shuffle status panel. A C++ UUserWidget drawn entirely in NativePaint, mirroring UrDead's
// in-game-confirmed UUrDeadReticle (UrDead/Source/UrDead/Public/UrDeadReticle.h + Private/UrDeadReticle.cpp): the same includes, the
// same CreateWidget + AddToViewport / RemoveFromParent route (UrDeadWeapon_Hud.cpp:145-159, :310) and the same draw calls
// (FSlateDrawElement::MakeLines with an explicit thickness for the backing, MakeText with the core default font). NO ASSET, NO UMG FILE,
// NO COOK: CreateWidget against StaticClass() is a native construction. Position, size and z-order: NodeShuffleStatus.h (Panel*).
// While a menu shows the cursor the widget draws NOTHING by an early return in NativePaint -- never by collapsing its visibility, which stops
// the widget ticking so it never comes back (UrDead census row 16, the LaserRifle POC's fix).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NodeShuffleStatusWidget.generated.h"

UCLASS()
class NODESHUFFLE_API UNodeShuffleStatusWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Pushed by the subsystem from the 5 s tick (or by NodeShuffle.StatusPanelTest). The widget composes no text and owns no state.
    FString Title;
    FString Body;
    FString Detail;

    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};
