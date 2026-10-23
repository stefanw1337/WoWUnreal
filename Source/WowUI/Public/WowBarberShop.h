#pragma once

#include "CoreMinimal.h"

/**
 * r354 (Stefan 15.10, "start working on the barber UI"): the barber shop session shared by the Lua
 * API the stock Blizzard_BarberShopUI calls (LuaBarberShopApi.cpp) and the gameplay controller,
 * which shows the chosen look on our own model while the window is open and moves the camera to
 * the face.
 *
 * 3.3.5a: sitting in a barber chair, the server sends SMSG_ENABLE_BARBER_SHOP (0x427, empty); the
 * client sends CMSG_ALTER_APPEARANCE (0x426: hair style, facial hair and skin as BarberShopStyle.dbc
 * ids, the hair colour as a colour index) and the server answers SMSG_BARBER_SHOP_RESULT (0x428),
 * changes PLAYER_BYTES / PLAYER_BYTES_2 and stands the player up.
 */
struct WOWUI_API FWowBarberShop
{
    /** SMSG_ENABLE_BARBER_SHOP arrived (the controller calls this, then fires BARBER_SHOP_OPEN). */
    static void NotifyOpened();
    /** SMSG_BARBER_SHOP_RESULT arrived. */
    static void NotifyResult();
    /** A barber shop session is open (the chair was taken and the window not yet left). */
    static bool IsOpen();
    /** The look to show on our own model while choosing; false = none (show the real fields). */
    static bool GetPreview(uint8& OutSkin, uint8& OutHairStyle, uint8& OutHairColor, uint8& OutFacialHair);
    /** Changes whenever the preview (or the session) changes. */
    static uint32 GetPreviewSerial();
};
