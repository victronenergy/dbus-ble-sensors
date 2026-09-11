#include "formatters.h"

#include <velib/base/ve_string.h>
#include <velib/nmea2k/n2k.h>
#include <velib/types/variant_print.h>
#include <velib/vecan/charger_error.h>

size_t formatters_state(VeVariant *var, const void *ctx, char* buf, size_t len)
{
	if (var->type.tp != VE_UN32) {
		return veVariantFmt(var, ctx, buf, len);
	}
	switch (var->value.UN32) {
	case N2K_CONVERTER_STATE_OFF:
		return ve_snprintf(buf, len, "%s", "Off");
	case N2K_CONVERTER_STATE_LOW_POWER_MODE:
		return ve_snprintf(buf, len, "%s", "Low power mode");
	case N2K_CONVERTER_STATE_FAULT:
		return ve_snprintf(buf, len, "%s", "Fault");
	case N2K_CONVERTER_STATE_BULK:
		return ve_snprintf(buf, len, "%s", "Bulk");
	case N2K_CONVERTER_STATE_ABSORPTION:
		return ve_snprintf(buf, len, "%s", "Absorption");
	case N2K_CONVERTER_STATE_FLOAT:
		return ve_snprintf(buf, len, "%s", "Float");
	case N2K_CONVERTER_STATE_STORAGE:
		return ve_snprintf(buf, len, "%s", "Storage");
	case N2K_CONVERTER_STATE_EQUALIZE:
		return ve_snprintf(buf, len, "%s", "Equalise");
	case N2K_CONVERTER_STATE_PASSTHRU:
		return ve_snprintf(buf, len, "%s", "Passthru");
	case N2K_CONVERTER_STATE_INVERTING:
		return ve_snprintf(buf, len, "%s", "Inverting");
	case N2K_CONVERTER_STATE_ASSISTING:
		return ve_snprintf(buf, len, "%s", "Assisting");
	case N2K_CONVERTER_STATE_PSU:
		return ve_snprintf(buf, len, "%s", "Power supply mode");
	case N2K_CONVERTER_STATE_SUSTAIN:
		return ve_snprintf(buf, len, "%s", "Sustain");
	case N2K_CONVERTER_STATE_WAKEUP:
		return ve_snprintf(buf, len, "%s", "Wakeup");
	case N2K_CONVERTER_STATE_REPEATED_ABSORPTION:
		return ve_snprintf(buf, len, "%s", "Repeated absorption");
	case N2K_CONVERTER_STATE_AUTO_EQUALIZE:
		return ve_snprintf(buf, len, "%s", "Auto equalize");
	case N2K_CONVERTER_STATE_BATTERYSAFE:
		return ve_snprintf(buf, len, "%s", "Battery safe");
	case N2K_CONVERTER_STATE_LOAD_DETECT:
		return ve_snprintf(buf, len, "%s", "Load detect");
	case N2K_CONVERTER_STATE_BLOCKED:
		return ve_snprintf(buf, len, "%s", "Blocked");
	case N2K_CONVERTER_STATE_TEST:
		return ve_snprintf(buf, len, "%s", "Test mode");
	case N2K_CONVERTER_STATE_EXTERNAL_CONTROL:
		return ve_snprintf(buf, len, "%s", "External control");
	case N2K_CONVERTER_STATE_UNAVAILABLE:
		return ve_snprintf(buf, len, "%s", "");
	}

	return ve_snprintf(buf, len, "%d", var->value.UN32);
}

const struct { int code; const char *description; } CHARGER_ERRORS[] =
{
	{ CHARGER_ERROR_NONE,					"No error" },
	{ CHARGER_ERROR_BATTERY_TEMP_TOO_HIGH,			"Battery high temperature" },
	{ CHARGER_ERROR_BATTERY_VOLTAGE_TOO_HIGH,		"Battery high voltage" },
	{ CHARGER_ERROR_BATTERY_TSENSE_PLUS_HIGH,		"Battery Tsense miswired" },
	{ CHARGER_ERROR_BATTERY_TSENSE_PLUS_LOW,		"Battery Tsense miswired" },
	{ CHARGER_ERROR_BATTERY_TSENSE_CONN_LOST,		"Battery Tsense missing" },
	{ CHARGER_ERROR_BATTERY_VSENSE_PLUS_LOW,		"Battery Vsense miswired" },
	{ CHARGER_ERROR_BATTERY_VSENSE_MIN_HIGH,		"Battery Vsense miswired" },
	{ CHARGER_ERROR_BATTERY_VSENSE_CONN_LOST,		"Battery Vsense missing" },
	{ CHARGER_ERROR_BATTERY_VSENSE_LOSSES,			"Battery high wire losses" },
	{ CHARGER_ERROR_BATTERY_VOLTAGE_TOO_LOW,		"Battery low voltage" },
	{ CHARGER_ERROR_BATTERY_RIPPLE_VOLTAGE,			"Battery high ripple voltage" },
	{ CHARGER_ERROR_BATTERY_LOW_SOC,			"Battery low state of charge" },
	{ CHARGER_ERROR_BATTERY_MIDPOINT_VOLTAGE,		"Battery mid-point voltage issue" },
	{ CHARGER_ERROR_BATTERY_TEMP_TOO_LOW,			"Battery temperature too low" },
	{ CHARGER_ERROR_BATTERY_RELAY_FAULT,			"Battery relay fault" },
	{ CHARGER_ERROR_BATTERY_NOT_FOUND,			"Battery not found" },
	{ CHARGER_ERROR_CHARGER_TEMP_TOO_HIGH,			"Charger high temperature" },
	{ CHARGER_ERROR_CHARGER_OVER_CURRENT,			"Charger excessive current" },
	{ CHARGER_ERROR_CHARGER_CURRENT_REVERSED,		"Charger negative current" },
	{ CHARGER_ERROR_CHARGER_BULKTIME_EXPIRED,		"Charger bulk time expired" },
	{ CHARGER_ERROR_CHARGER_CURRENT_SENSE,			"Charger current sensor issue" },
	{ CHARGER_ERROR_CHARGER_TSENSE_SHORT,			"Internal Tsensor miswired" },
	{ CHARGER_ERROR_CHARGER_TSENSE_CONN_LOST,		"Internal Tsensor missing" },
	{ CHARGER_ERROR_CHARGER_FAN_MISSING,			"Charger fan not detected" },
	{ CHARGER_ERROR_CHARGER_FAN_OVER_CURRENT,		"Charger fan over-current" },
	{ CHARGER_ERROR_CHARGER_TERMINAL_OVERHEAT,		"Charger terminal overheat" },
	{ CHARGER_ERROR_CHARGER_SHORT_CIRCUIT,			"Charger short circuit" },
	{ CHARGER_ERROR_CHARGER_CONVERTER_ISSUE,		"Charger power stage issue" },
	{ CHARGER_ERROR_CHARGER_OVER_CHARGE,			"Over-charge protection" },
	{ CHARGER_ERROR_INPUT_VOLTAGE_TOO_HIGH,			"Input high voltage" },
	{ CHARGER_ERROR_INPUT_OVER_CURRENT,			"Input excessive current" },
	{ CHARGER_ERROR_INPUT_OVER_POWER,			"Input excessive power" },
	{ CHARGER_ERROR_INPUT_POLARITY,				"Input polarity issue" },
	{ CHARGER_ERROR_INPUT_VOLTAGE_ABSENT,			"Input voltage absent" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN,				"Input shutdown (no retries)" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_RETRY,			"Input shutdown (retry)" },
	{ CHARGER_ERROR_INTERNAL_FAILURE,			"Input internal failure" },
	{ CHARGER_ERROR_PVRISO_FAULT,				"PV isolation failure" },
	{ CHARGER_ERROR_GFCI_FAULT,				"PV isolation failure" },
	{ CHARGER_ERROR_GROUND_RELAY_FAULT,			"Ground fault detected" },
	{ CHARGER_ERROR_INVERTER_OVERLOAD,			"Inverter overload" },
	{ CHARGER_ERROR_INVERTER_TEMP_TOO_HIGH,			"Inverter temp too high" },
	{ CHARGER_ERROR_INVERTER_OVER_CURRENT,			"Inverter peak current" },
	{ CHARGER_ERROR_INVERTER_DC_LEVEL,			"Inverter internal DC level" },
	{ CHARGER_ERROR_INVERTER_AC_LEVEL,			"Inverter wrong ACout level" },
	{ CHARGER_ERROR_INVERTER_DC_FAIL,			"Inverter powerstage fault" },
	{ CHARGER_ERROR_INVERTER_AC_FAIL,			"Inverter powerstage fault" },
	{ CHARGER_ERROR_INVERTER_AC_ON_OUTPUT,			"Inverter connected to AC" },
	{ CHARGER_ERROR_INVERTER_BRIDGE_FAULT,			"Inverter powerstage fault" },
	{ CHARGER_ERROR_ACIN1_RELAY_FAULT,			"ACIN1 relay test fault" },
	{ CHARGER_ERROR_ACIN2_RELAY_FAULT,			"ACIN2 relay test fault" },
	{ CHARGER_ERROR_LINK_DEVICE_MISSING,			"Device disappeared" },
	{ CHARGER_ERROR_LINK_CONFIGURATION,			"Incompatible device" },
	{ CHARGER_ERROR_LINK_BMS_MISSING,			"BMS connection lost" },
	{ CHARGER_ERROR_LINK_CONFIG_MISMATCH,			"Network misconfigured" },
	{ CHARGER_ERROR_LINK_SETTINGS_ISSUE,			"Network misconfigured" },
	{ CHARGER_ERROR_LINK_DEVICE_NOT_CAPABLE,		"Network misconfigured" },
	{ CHARGER_ERROR_LINK_PROTOCOL_VERSION,			"Network misconfigured" },
	{ CHARGER_ERROR_LINK_PHASE_ROTATION,			"Phase rotation" },
	{ CHARGER_ERROR_LINK_MULTIPLE_AC_INPUTS,		"Multiple AC inputs" },
	{ CHARGER_ERROR_LINK_PHASE_OVERLOAD,			"Too many units in parallel" },
	{ CHARGER_ERROR_LINK_CONFIG_NOT_SUPPORTED,		"Network misconfigured" },
	{ CHARGER_ERROR_LINK_NETWORK_INCOMPLETE,		"Network incomplete" },
	{ CHARGER_ERROR_LINK_SETTINGS_SYNC_ISSUE,		"Settings sync disabled" },
	{ CHARGER_ERROR_MEMORY_WRITE_FAILURE,			"Memory write error" },
	{ CHARGER_ERROR_CPU_TEMP_TOO_HIGH,			"CPU temperature too high" },
	{ CHARGER_ERROR_COMMUNICATION_LOST,			"Communication lost" },
	{ CHARGER_ERROR_CALIBRATION_DATA_LOST,			"Calibration data lost" },
	{ CHARGER_ERROR_INVALID_FIRMWARE,			"Incompatible firmware" },
	{ CHARGER_ERROR_INVALID_HARDWARE,			"Incompatible hardware" },
	{ CHARGER_ERROR_SETTINGS_DATA_INVALID,			"Settings invalid" },
	{ CHARGER_ERROR_REFERENCE_VOLTAGE_FAILURE,		"Reference voltage failed" },
	{ CHARGER_ERROR_TESTER_FAIL,				"Tester fail" },
	{ CHARGER_ERROR_HISTORY_DATA_INVALID,			"History invalid" },
	{ CHARGER_ERROR_KWH_COUNTERS_INVALID,			"kWh counters invalid" },
	{ CHARGER_ERROR_INTERNAL_UNDERVOLTAGE_HV,		"DC voltage error" },
	{ CHARGER_ERROR_INTERNAL_DCDC_FAILURE,			"DC voltage error" },
	{ CHARGER_ERROR_INTERNAL_GFCI_FAILURE,			"GFCI sensor error" },
	{ CHARGER_ERROR_INTERNAL_UNDERVOLTAGE_3V3,		"3V3 supply error" },
	{ CHARGER_ERROR_INTERNAL_UNDERVOLTAGE_5V,		"5V supply error" },
	{ CHARGER_ERROR_INTERNAL_UNDERVOLTAGE_12V,		"12V supply error" },
	{ CHARGER_ERROR_INTERNAL_UNDERVOLTAGE_15V,		"15V supply error" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_OVERVOLT_MAN_RST,	"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_FAST_OVERVOLT_MAN_RST,	"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_HIGH_OVERVOLT_MAN_RST,	"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_OFF_CURRENT_MAN_RST,	"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_OVERVOLT,		"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_FAST_OVERVOLT,		"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_HIGH_OVERVOLT,		"PV Input shutdown" },
	{ CHARGER_ERROR_INPUT_SHUTDOWN_OFF_CURRENT,		"PV Input shutdown" },
	{ CHARGER_ERROR_BATTERY_TEMP_TOO_HIGH_WARN,		"Battery high temperature" },
	{ CHARGER_ERROR_BATTERY_TEMP_TOO_LOW_WARN,		"Battery temperature too low" },
	{ CHARGER_ERROR_CHARGER_TEMP_TOO_HIGH_WARN,		"Charger high temperature" },
	{ CHARGER_ERROR_CHARGER_SHORT_CIRCUIT_WARN,		"Charger short circuit" },
	{ CHARGER_ERROR_CHARGER_CONVERTER_ISSUE_WARN,		"Charger power stage issue" },
	{ CHARGER_ERROR_PRECHARGE_FAILED,			"Charger pre-charge failed" },
	{ CHARGER_ERROR_SOLAR_RELAY_FAULT,			"Charger solar relay failed" },
	{ CHARGER_ERROR_SOLAR_SHORT,				"Charger solar short" },
	{ CHARGER_ERROR_SOLAR_CONNECT_FAILED,			"Charger solar connect failed" },
	{ CHARGER_ERROR_UNKNOWN,				"Unknown error" },
};

/* This array contains charger errors that are currently unused. When a new error is added,
 * the define will be removed and the following will thus not compile. This is a trigger to
 * add the new error to the list above. */
const int UNDEFINED_CHARGER_ERRORS[] = {
	CHARGER_ERROR_30,
	CHARGER_ERROR_61, CHARGER_ERROR_62, CHARGER_ERROR_63, CHARGER_ERROR_64,
	CHARGER_ERROR_78, CHARGER_ERROR_79,
	CHARGER_ERROR_88, CHARGER_ERROR_89,
	CHARGER_ERROR_90, CHARGER_ERROR_91, CHARGER_ERROR_92, CHARGER_ERROR_93, CHARGER_ERROR_94,
	CHARGER_ERROR_95, CHARGER_ERROR_96, CHARGER_ERROR_97, CHARGER_ERROR_98, CHARGER_ERROR_99,
	CHARGER_ERROR_100, CHARGER_ERROR_101, CHARGER_ERROR_102, CHARGER_ERROR_103, CHARGER_ERROR_104,
	CHARGER_ERROR_105, CHARGER_ERROR_106, CHARGER_ERROR_107, CHARGER_ERROR_108, CHARGER_ERROR_109,
	CHARGER_ERROR_110, CHARGER_ERROR_111, CHARGER_ERROR_112,
	CHARGER_ERROR_124, 
	CHARGER_ERROR_125, CHARGER_ERROR_126, CHARGER_ERROR_127, CHARGER_ERROR_128, CHARGER_ERROR_129,
	CHARGER_ERROR_130, CHARGER_ERROR_131, CHARGER_ERROR_132, CHARGER_ERROR_133, CHARGER_ERROR_134,
	CHARGER_ERROR_135, CHARGER_ERROR_136, CHARGER_ERROR_137, CHARGER_ERROR_138, CHARGER_ERROR_139,
	CHARGER_ERROR_140, CHARGER_ERROR_141, CHARGER_ERROR_142, CHARGER_ERROR_143, CHARGER_ERROR_144,
	CHARGER_ERROR_145, CHARGER_ERROR_146, CHARGER_ERROR_147, CHARGER_ERROR_148, CHARGER_ERROR_149,
	CHARGER_ERROR_152, CHARGER_ERROR_153, CHARGER_ERROR_154,
	CHARGER_ERROR_155, CHARGER_ERROR_156, CHARGER_ERROR_157, CHARGER_ERROR_158, CHARGER_ERROR_159,
	CHARGER_ERROR_163, CHARGER_ERROR_164,
	CHARGER_ERROR_165, CHARGER_ERROR_166, CHARGER_ERROR_167, CHARGER_ERROR_168, CHARGER_ERROR_169,
	CHARGER_ERROR_170, CHARGER_ERROR_171, CHARGER_ERROR_172, CHARGER_ERROR_173, CHARGER_ERROR_174,
	CHARGER_ERROR_175, CHARGER_ERROR_176, CHARGER_ERROR_177, CHARGER_ERROR_178, CHARGER_ERROR_179,
	CHARGER_ERROR_181, CHARGER_ERROR_182, CHARGER_ERROR_183, CHARGER_ERROR_184,
	CHARGER_ERROR_185, CHARGER_ERROR_186, CHARGER_ERROR_187, CHARGER_ERROR_188, CHARGER_ERROR_189,
	CHARGER_ERROR_190, CHARGER_ERROR_191, CHARGER_ERROR_192, CHARGER_ERROR_193, CHARGER_ERROR_194,
	CHARGER_ERROR_195, CHARGER_ERROR_196, CHARGER_ERROR_197, CHARGER_ERROR_198, CHARGER_ERROR_199,
	CHARGER_ERROR_204,
	CHARGER_ERROR_206, CHARGER_ERROR_207, CHARGER_ERROR_208, CHARGER_ERROR_209,
	CHARGER_ERROR_210, CHARGER_ERROR_211, CHARGER_ERROR_213, CHARGER_ERROR_214,
	CHARGER_ERROR_216, CHARGER_ERROR_217, CHARGER_ERROR_218, CHARGER_ERROR_219,
	CHARGER_ERROR_223, CHARGER_ERROR_224,
	CHARGER_ERROR_225, CHARGER_ERROR_226, CHARGER_ERROR_227, CHARGER_ERROR_228, CHARGER_ERROR_229,
	CHARGER_ERROR_230, CHARGER_ERROR_231, CHARGER_ERROR_232, CHARGER_ERROR_233, CHARGER_ERROR_234,
	CHARGER_ERROR_235, CHARGER_ERROR_236, CHARGER_ERROR_237, CHARGER_ERROR_238, CHARGER_ERROR_239,
	CHARGER_ERROR_240, CHARGER_ERROR_241, CHARGER_ERROR_242, CHARGER_ERROR_243, CHARGER_ERROR_244,
	CHARGER_ERROR_245, CHARGER_ERROR_246, CHARGER_ERROR_247, CHARGER_ERROR_248, CHARGER_ERROR_249,
	CHARGER_ERROR_250, CHARGER_ERROR_251, CHARGER_ERROR_252, CHARGER_ERROR_253, CHARGER_ERROR_254,
};

size_t formatters_charger_error(VeVariant *var, const void *ctx, char* buf, size_t len)
{
	VE_UNUSED(UNDEFINED_CHARGER_ERRORS);
	if (var->type.tp != VE_UN32) {
		return veVariantFmt(var, ctx, buf, len);
	}
	uint32_t error = var->value.UN32;
	for (size_t i = 0; i < sizeof(CHARGER_ERRORS) / sizeof(CHARGER_ERRORS[0]); i++) {
		if (CHARGER_ERRORS[i].code == error) {
			return ve_snprintf(buf, len, "%s", CHARGER_ERRORS[i].description);
		}
	}
	return ve_snprintf(buf, len, "%d", var->value.UN32);
}
