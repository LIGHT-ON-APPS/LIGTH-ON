/*
 * gui.c
 *
 *  Created on: Jul 13, 2026
 *      Author: young
 */


#include "gui.h"


#if defined(_USE_HW_LCD)
#define GUI_PERIOD_MS  1000U
#define GUI_LINE_H     18
#define GUI_LINES      12

static bool     gui_enable;
static bool     gui_initialized;
static uint32_t gui_pre_time;

static void guiLcdUpdate(bool force);
static void guiClearLines(void);
static int32_t guiAbsI32(int32_t value);

#ifdef _USE_HW_CLI
static void cliGui(cli_args_t *args);
#endif
#endif


void guiInit(void)
{
#if defined(_USE_HW_LCD) && defined(_USE_HW_CLI)
  (void)cliAdd("gui", cliGui);
#endif
}

void guiUpdate(void)
{
#if defined(_USE_HW_LCD)
  guiLcdUpdate(false);
#endif
}

#if defined(_USE_HW_LCD)
static int32_t guiAbsI32(int32_t value)
{
  return (value < 0) ? -value : value;
}

static void guiClearLine(uint8_t line)
{
  lcdDrawFillRect(0, (int16_t)line * GUI_LINE_H,
                  HW_LCD_WIDTH, GUI_LINE_H, black);
}

static void guiClearLines(void)
{
  uint8_t i;

  for (i = 0; i < GUI_LINES; i++)
  {
    guiClearLine(i);
  }
}

static void guiPrintMv(uint8_t line, const char *label, int32_t mv,
                       bool valid, uint16_t color)
{
  guiClearLine(line);

  if (valid == true)
  {
    lcdPrintf(0, line * GUI_LINE_H, color, "%-8s %ld.%03ld V",
              label,
              (long)(mv / 1000L),
              (long)guiAbsI32(mv % 1000L));
  }
  else
  {
    lcdPrintf(0, line * GUI_LINE_H, gray, "%-8s ---.- V", label);
  }
}

static void guiPrintMa(uint8_t line, const char *label, int32_t ma,
                       bool valid, uint16_t color)
{
  guiClearLine(line);

  if (valid == true)
  {
    lcdPrintf(0, line * GUI_LINE_H, color, "%-8s %ld.%01ld A",
              label,
              (long)(ma / 1000L),
              (long)(guiAbsI32(ma % 1000L) / 100L));
  }
  else
  {
    lcdPrintf(0, line * GUI_LINE_H, gray, "%-8s ---.- A", label);
  }
}

static void guiPrintDeci(uint8_t line, const char *label, int32_t deci,
                         bool valid, const char *unit, uint16_t color)
{
  guiClearLine(line);

  if (valid == true)
  {
    lcdPrintf(0, line * GUI_LINE_H, color, "%-8s %ld.%ld %s",
              label,
              (long)(deci / 10L),
              (long)guiAbsI32(deci % 10L),
              unit);
  }
  else
  {
    lcdPrintf(0, line * GUI_LINE_H, gray, "%-8s ---.- %s", label, unit);
  }
}

static void guiPrintI32(uint8_t line, const char *label, int32_t value,
                        bool valid, const char *unit, uint16_t color)
{
  guiClearLine(line);

  if (valid == true)
  {
    lcdPrintf(0, line * GUI_LINE_H, color, "%-8s %ld %s",
              label, (long)value, unit);
  }
  else
  {
    lcdPrintf(0, line * GUI_LINE_H, gray, "%-8s --- %s", label, unit);
  }
}

static void guiLcdUpdate(bool force)
{
  telemetry_signal_snapshot_t signal;
  uint32_t now_ms;
  uint32_t age_ms;
  bool has_signal;

  if ((gui_enable != true) && (force != true))
  {
    return;
  }

  now_ms = millis();
  if ((force != true) && ((now_ms - gui_pre_time) < GUI_PERIOD_MS))
  {
    return;
  }
  gui_pre_time = now_ms;

  if (gui_initialized != true)
  {
    guiClearLines();
    gui_initialized = true;
  }

  has_signal = telemetryGetSignalSnapshot(&signal);
  age_ms = (signal.last_update_ms == 0U) ? 0U : (now_ms - signal.last_update_ms);

  guiClearLine(0);
  lcdPrintf(0, 0, green, "LCD NUM GUI  %lu ms", (unsigned long)now_ms);

  guiClearLine(1);
  lcdPrintf(0, GUI_LINE_H, has_signal ? white : orange,
            "valid 0x%08lX age %lums",
            (unsigned long)signal.valid_mask,
            (unsigned long)age_ms);

  guiPrintI32(2, "RPM", signal.bamocar_rpm,
              (signal.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_RPM) != 0U,
              "rpm", lightblue);
  guiPrintMv(3, "VOUT", signal.bamocar_dc_bus_mv,
             (signal.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_DC_BUS_MV) != 0U,
             lightblue);
  guiPrintMv(4, "PACK V", signal.bms_pack_mv,
             (signal.valid_mask & TELEMETRY_SIGNAL_BMS_PACK_MV) != 0U,
             white);
  guiPrintMa(5, "PACK A", signal.bms_pack_ma,
             (signal.valid_mask & TELEMETRY_SIGNAL_BMS_PACK_MA) != 0U,
             white);
  guiPrintDeci(6, "SOC", signal.bms_soc_permille,
               (signal.valid_mask & TELEMETRY_SIGNAL_BMS_SOC_PERMILLE) != 0U,
               "%", green);
  guiPrintDeci(7, "MOTOR", signal.bamocar_motor_temp_decic,
               (signal.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_MOTOR_TEMP_DECIC) != 0U,
               "C", yellow);
  guiPrintDeci(8, "IGBT", signal.bamocar_igbt_temp_decic,
               (signal.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_IGBT_TEMP_DECIC) != 0U,
               "C", green);
  guiPrintI32(9, "PHASE I", signal.bamocar_torque_raw,
              (signal.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_TORQUE_RAW) != 0U,
              "raw", white);

  guiClearLine(10);
  lcdPrintf(0, 10 * GUI_LINE_H, gray, "can %u  log %u",
            (unsigned int)signal.can_ch,
            signal.logging ? 1U : 0U);
}

#ifdef _USE_HW_CLI
static void cliGui(cli_args_t *args)
{
  bool ret = false;

  if ((args->argc == 1) && args->isStr(0, "on"))
  {
    gui_enable = true;
    gui_initialized = false;
    guiLcdUpdate(true);
    cliPrintf("gui on\n");
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "off"))
  {
    gui_enable = false;
    cliPrintf("gui off\n");
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "once"))
  {
    guiLcdUpdate(true);
    cliPrintf("gui once\n");
    ret = true;
  }

  if ((args->argc == 1) && args->isStr(0, "clear"))
  {
    guiClearLines();
    gui_initialized = false;
    cliPrintf("gui clear\n");
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("gui on\n");
    cliPrintf("gui off\n");
    cliPrintf("gui once\n");
    cliPrintf("gui clear\n");
  }
}
#endif
#endif

