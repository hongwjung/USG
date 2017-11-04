/*
 * upstream_hid_botdetect.c
 *
 *  Created on: Aug 17, 2017
 *      Author: Robert Fisk
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
 
 
#include "upstream_hid_botdetect.h"
#include "upstream_hid.h"
#include "build_config.h"



//Variables common between keyboard and mouse bot detection:
uint32_t                        TemporaryLockoutTimeMs;
volatile LockoutStateTypeDef    LockoutState = LOCKOUT_STATE_INACTIVE;



//Variables specific to keyboard bot detection:
#if defined (CONFIG_KEYBOARD_ENABLED) && defined (CONFIG_KEYBOARD_BOT_DETECT_ENABLED)
    uint32_t            LastKeyDownTime = 0;
    KeyTimerLogTypeDef  KeyTimerLog[KEYBOARD_BOTDETECT_MAX_ACTIVE_KEYS] = {0};

    uint8_t             KeyDelayFastBinDrainDivideCount     = 0;
    uint8_t             KeyDelaySlowBinDrainDivideCount     = 0;
    uint8_t             KeyDowntimeFastBinDrainDivideCount  = 0;
    uint8_t             KeyDowntimeSlowBinDrainDivideCount  = 0;
    uint8_t             KeyDelayFastBinArray[KEYBOARD_BOTDETECT_FAST_BIN_COUNT]     = {0};
    uint8_t             KeyDelaySlowBinArray[KEYBOARD_BOTDETECT_SLOW_BIN_COUNT]     = {0};
    uint8_t             KeyDowntimeFastBinArray[KEYBOARD_BOTDETECT_FAST_BIN_COUNT]  = {0};
    uint8_t             KeyDowntimeSlowBinArray[KEYBOARD_BOTDETECT_SLOW_BIN_COUNT]  = {0};
    uint8_t             OldKeyboardInData[HID_KEYBOARD_INPUT_DATA_LEN]              = {0};

    //Debug:
//    uint8_t             KeyDelayFastBinArrayPeak;
//    uint8_t             KeyDelaySlowBinArrayPeak;
//    uint8_t             KeyDowntimeFastBinArrayPeak;
//    uint8_t             KeyDowntimeSlowBinArrayPeak;
#endif



//Variables specific to mouse bot detection:
#if defined (CONFIG_MOUSE_ENABLED) && defined (CONFIG_MOUSE_BOT_DETECT_ENABLED)

#endif



//Code specific to keyboard bot detection:
#if defined (CONFIG_KEYBOARD_ENABLED) && defined (CONFIG_KEYBOARD_BOT_DETECT_ENABLED)

static uint32_t Upstream_HID_BotDetectKeyboard_RolloverCheck(uint8_t* keyboardInData);
static void     Upstream_HID_BotDetectKeyboard_DoLockout(void);
static void     Upstream_HID_BotDetectKeyboard_KeyDown(uint8_t keyCode);
static void     Upstream_HID_BotDetectKeyboard_KeyUp(uint8_t keyCode);



//Checks if received keyboard data is from a real human.
//This is not entirely bulletproof as an attacking device may randomize its keypresses.
void Upstream_HID_BotDetectKeyboard(uint8_t* keyboardInData)
{
    uint32_t i;
    uint32_t j;
    uint8_t  tempModifier;

    if (Upstream_HID_BotDetectKeyboard_RolloverCheck(keyboardInData)) return;

    //Process modifier keys in first byte
    tempModifier = keyboardInData[0];
    for (i = 0; i < 8; i++)
    {
        if ((tempModifier & 1) && !(OldKeyboardInData[0] & 1))
        {
            Upstream_HID_BotDetectKeyboard_KeyDown(KEY_MODIFIER_BASE + i);
        }
        if (!(tempModifier & 1) && (OldKeyboardInData[0] & 1))
        {
            Upstream_HID_BotDetectKeyboard_KeyUp(KEY_MODIFIER_BASE + i);
        }
        tempModifier >>= 1;
        OldKeyboardInData[0] >>= 1;
    }


    //Process key array: search for keydowns
    for (i = 2; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
    {
        if (keyboardInData[i] >= KEY_A)
        {
            for (j = 2; j < HID_KEYBOARD_INPUT_DATA_LEN; j++)
            {
                if (keyboardInData[i] == OldKeyboardInData[j]) break;
            }
            if (j >= HID_KEYBOARD_INPUT_DATA_LEN)
            {
                Upstream_HID_BotDetectKeyboard_KeyDown(keyboardInData[i]);
            }
        }
    }

    //Process key array: search for keyups
     for (i = 2; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
     {
         if (OldKeyboardInData[i] >= KEY_A)
         {
             for (j = 2; j < HID_KEYBOARD_INPUT_DATA_LEN; j++)
             {
                 if (OldKeyboardInData[i] == keyboardInData[j]) break;
             }
             if (j >= HID_KEYBOARD_INPUT_DATA_LEN)
             {
                 Upstream_HID_BotDetectKeyboard_KeyUp(OldKeyboardInData[i]);
             }
         }
     }

    //Check for evidence of bot typing
    for (i = 0; i < KEYBOARD_BOTDETECT_FAST_BIN_COUNT; i++)
    {
        if ((KeyDelayFastBinArray[i]    > KEYBOARD_BOTDETECT_TEMPORARY_LOCKOUT_BIN_THRESHOLD) ||
            (KeyDowntimeFastBinArray[i] > KEYBOARD_BOTDETECT_TEMPORARY_LOCKOUT_BIN_THRESHOLD))
        {
            Upstream_HID_BotDetectKeyboard_DoLockout();
            break;
        }

        //Debug:
//        if (KeyDelayFastBinArray[i]    > KeyDelayFastBinArrayPeak)    KeyDelayFastBinArrayPeak = KeyDelayFastBinArray[i];
//        if (KeyDowntimeFastBinArray[i] > KeyDowntimeFastBinArrayPeak) KeyDowntimeFastBinArrayPeak = KeyDowntimeFastBinArray[i];
    }
    for (i = 0; i < KEYBOARD_BOTDETECT_SLOW_BIN_COUNT; i++)
    {
        if ((KeyDelaySlowBinArray[i]    > KEYBOARD_BOTDETECT_TEMPORARY_LOCKOUT_BIN_THRESHOLD) ||
            (KeyDowntimeSlowBinArray[i] > KEYBOARD_BOTDETECT_TEMPORARY_LOCKOUT_BIN_THRESHOLD))
        {
            Upstream_HID_BotDetectKeyboard_DoLockout();
            break;
        }

        //Debug:
//        if (KeyDelaySlowBinArray[i]    > KeyDelaySlowBinArrayPeak)    KeyDelaySlowBinArrayPeak = KeyDelaySlowBinArray[i];
//        if (KeyDowntimeSlowBinArray[i] > KeyDowntimeSlowBinArrayPeak) KeyDowntimeSlowBinArrayPeak = KeyDowntimeSlowBinArray[i];
    }

    //Copy new data to old array
    for (i = 0; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
    {
        OldKeyboardInData[i] = keyboardInData[i];
    }

    //Host receives no data if we are locked
    if ((LockoutState == LOCKOUT_STATE_TEMPORARY_ACTIVE) ||
        (LockoutState == LOCKOUT_STATE_PERMANENT_ACTIVE))
    {
        for (i = 0; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
        {
            keyboardInData[i] = 0;
        }
    }
}



static void Upstream_HID_BotDetectKeyboard_DoLockout(void)
{
    uint32_t i;

    if (LockoutState == LOCKOUT_STATE_PERMANENT_ACTIVE) return;

    //Are we already in warning state? -> activate permanent lockout
    if ((LockoutState == LOCKOUT_STATE_TEMPORARY_ACTIVE) ||
        (LockoutState == LOCKOUT_STATE_TEMPORARY_FLASHING))
    {
        LockoutState = LOCKOUT_STATE_PERMANENT_ACTIVE;
        return;
    }

    //Otherwise, reset counters and give warning
    for (i = 0; i < KEYBOARD_BOTDETECT_FAST_BIN_COUNT; i++)
    {
        KeyDelayFastBinArray[i] = 0;
        KeyDowntimeFastBinArray[i] = 0;
    }
    for (i = 0; i < KEYBOARD_BOTDETECT_SLOW_BIN_COUNT; i++)
    {
        KeyDelaySlowBinArray[i] = 0;
        KeyDowntimeSlowBinArray[i] = 0;
    }

    TemporaryLockoutTimeMs = 0;
    LockoutState = LOCKOUT_STATE_TEMPORARY_ACTIVE;
    LED_SetState(LED_STATUS_FLASH_BOTDETECT);
}



//Keyboard reports a rollover code when there are too many keys to scan/report.
static uint32_t Upstream_HID_BotDetectKeyboard_RolloverCheck(uint8_t* keyboardInData)
{
    uint32_t i;

    for (i = 2; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
    {
        if (keyboardInData[i] == KEY_ROLLOVER) break;
    }
    if (i >= HID_KEYBOARD_INPUT_DATA_LEN) return 0;

    //As I am unclear on the exact usage and interpretation of the rollover code,
    //we are going to play it safe by copying the old keyboard data over the new array.
    //This ensures the host interprets a rollover event exactly the way we do!

    //Host receives no data if we are locked
    if ((LockoutState == LOCKOUT_STATE_TEMPORARY_ACTIVE) ||
        (LockoutState == LOCKOUT_STATE_PERMANENT_ACTIVE))
    {
        for (i = 0; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
        {
            keyboardInData[i] = 0;
        }
    }
    else
    {
        for (i = 0; i < HID_KEYBOARD_INPUT_DATA_LEN; i++)
        {
            keyboardInData[i] = OldKeyboardInData[i];
        }
    }
    return 1;
}



static void Upstream_HID_BotDetectKeyboard_KeyDown(uint8_t keyCode)
{
    uint32_t i;
    uint32_t keyDelay;
    uint32_t now = HAL_GetTick();

    keyDelay = now - LastKeyDownTime;
    if (keyDelay < (KEYBOARD_BOTDETECT_FAST_BIN_WIDTH_MS * KEYBOARD_BOTDETECT_FAST_BIN_COUNT))
    {
        KeyDelayFastBinArray[(keyDelay / KEYBOARD_BOTDETECT_FAST_BIN_WIDTH_MS)]++;                          //Add key to fast bin

        //Drain fast bins at specified rate
        KeyDelayFastBinDrainDivideCount++;
        if (KeyDelayFastBinDrainDivideCount >= KEYBOARD_BOTDETECT_FAST_BIN_DRAIN_DIVIDER)
        {
            KeyDelayFastBinDrainDivideCount = 0;
            for (i = 0; i < KEYBOARD_BOTDETECT_FAST_BIN_COUNT; i++)
            {
                if (KeyDelayFastBinArray[i] > 0) KeyDelayFastBinArray[i]--;
            }
        }
    }
    else
    {
        keyDelay = keyDelay % (KEYBOARD_BOTDETECT_SLOW_BIN_WIDTH_MS * KEYBOARD_BOTDETECT_SLOW_BIN_COUNT);   //Wrap slow key time into the slow array
        KeyDelaySlowBinArray[(keyDelay / KEYBOARD_BOTDETECT_SLOW_BIN_WIDTH_MS)]++;                          //Add key to slow bin

        //Drain slow bins at specified rate
        KeyDelaySlowBinDrainDivideCount++;
        if (KeyDelaySlowBinDrainDivideCount >= KEYBOARD_BOTDETECT_SLOW_BIN_DRAIN_DIVIDER)
        {
            KeyDelaySlowBinDrainDivideCount = 0;
            for (i = 0; i < KEYBOARD_BOTDETECT_SLOW_BIN_COUNT; i++)
            {
                if (KeyDelaySlowBinArray[i] > 0) KeyDelaySlowBinArray[i]--;
            }
        }
    }
    LastKeyDownTime = now;

    for (i = 0; i < KEYBOARD_BOTDETECT_MAX_ACTIVE_KEYS; i++)
    {
        if (KeyTimerLog[i].KeyCode == 0) break;
    }
    if (i >= KEYBOARD_BOTDETECT_MAX_ACTIVE_KEYS) while (1);         //Totally should not happen
    KeyTimerLog[i].KeyCode = keyCode;
    KeyTimerLog[i].KeyDownStart = now;
}




static void Upstream_HID_BotDetectKeyboard_KeyUp(uint8_t keyCode)
{
    uint32_t i;
    uint32_t keyDowntime;

    for (i = 0; i < KEYBOARD_BOTDETECT_MAX_ACTIVE_KEYS; i++)
    {
        if (KeyTimerLog[i].KeyCode == keyCode) break;
    }
    if (i >= KEYBOARD_BOTDETECT_MAX_ACTIVE_KEYS) while (1);         //Totally should not happen

    KeyTimerLog[i].KeyCode = 0;                                     //Clear out the key entry
    keyDowntime = HAL_GetTick() - KeyTimerLog[i].KeyDownStart;
    if (keyDowntime < (KEYBOARD_BOTDETECT_FAST_BIN_WIDTH_MS * KEYBOARD_BOTDETECT_FAST_BIN_COUNT))
    {
        KeyDowntimeFastBinArray[(keyDowntime / KEYBOARD_BOTDETECT_FAST_BIN_WIDTH_MS)]++;                          //Add key to fast bin

        //Drain fast bins at specified rate
        KeyDowntimeFastBinDrainDivideCount++;
        if (KeyDowntimeFastBinDrainDivideCount >= KEYBOARD_BOTDETECT_FAST_BIN_DRAIN_DIVIDER)
        {
            KeyDowntimeFastBinDrainDivideCount = 0;
            for (i = 0; i < KEYBOARD_BOTDETECT_FAST_BIN_COUNT; i++)
            {
                if (KeyDowntimeFastBinArray[i] > 0) KeyDowntimeFastBinArray[i]--;
            }
        }
    }
    else
    {
        keyDowntime = keyDowntime % (KEYBOARD_BOTDETECT_SLOW_BIN_WIDTH_MS * KEYBOARD_BOTDETECT_SLOW_BIN_COUNT);   //Wrap slow key time into the slow array
        KeyDowntimeSlowBinArray[(keyDowntime / KEYBOARD_BOTDETECT_SLOW_BIN_WIDTH_MS)]++;                          //Add key to slow bin

        //Drain slow bins at specified rate
        KeyDowntimeSlowBinDrainDivideCount++;
        if (KeyDowntimeSlowBinDrainDivideCount >= KEYBOARD_BOTDETECT_SLOW_BIN_DRAIN_DIVIDER)
        {
            KeyDowntimeSlowBinDrainDivideCount = 0;
            for (i = 0; i < KEYBOARD_BOTDETECT_SLOW_BIN_COUNT; i++)
            {
                if (KeyDowntimeSlowBinArray[i] > 0) KeyDowntimeSlowBinArray[i]--;
            }
        }
    }
}

#endif  //if defined (CONFIG_KEYBOARD_ENABLED) && defined (CONFIG_KEYBOARD_BOT_DETECT_ENABLED)





//Called by Systick_Handler every 1ms, at high interrupt priority.
void Upstream_HID_BotDetect_Systick(void)
{
//Keyboard-specific stuff:
#if defined (CONFIG_KEYBOARD_ENABLED) && defined (CONFIG_KEYBOARD_BOT_DETECT_ENABLED)
    //Check if temporary lockout has expired
    if (LockoutState == LOCKOUT_STATE_TEMPORARY_ACTIVE)
    {
        if (TemporaryLockoutTimeMs++ > KEYBOARD_BOTDETECT_TEMPORARY_LOCKOUT_TIME_MS)
        {
            LockoutState = LOCKOUT_STATE_TEMPORARY_FLASHING;
        }
    }
    else if (LockoutState == LOCKOUT_STATE_TEMPORARY_FLASHING)
    {
        if (TemporaryLockoutTimeMs++ > KEYBOARD_BOTDETECT_TEMPORARY_LOCKOUT_FLASH_TIME_MS)
        {
            LED_SetState(LED_STATUS_OFF);
            LockoutState = LOCKOUT_STATE_INACTIVE;
        }
    }
#endif


#if defined (CONFIG_MOUSE_ENABLED) && defined (CONFIG_MOUSE_BOT_DETECT_ENABLED)

#endif
}



