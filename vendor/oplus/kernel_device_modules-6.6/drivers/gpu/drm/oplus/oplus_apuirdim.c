/***************************************************************
** Copyright (C), 2025, OPLUS Mobile Comm Corp., Ltd
** File : oplus_apuirdim.c
** Description : oplus_apuirdim header
** Version : 2.0
** Date : 2025/06/15
** Author : Display
***************************************************************/
#include "oplus_apuirdim.h"
#include <linux/of_gpio.h>
#include "oplus_display_debug.h"
#include "mtk_drm_drv.h"
#include "mtk_drm_crtc.h"
#include "mtk_log.h"
#include "mtk_drm_mmp.h"
#include "mtk_dsi.h"
#include "oplus_dsi_display_config.h"
#include "oplus_dsi_panel_cmd.h"
#include <linux/math.h>

enum oplus_apuir_log_level {
	OPLUS_APUIR_LOG_LEVEL_NONE = 0,
	OPLUS_APUIR_LOG_LEVEL_ERR = 1,
	OPLUS_APUIR_LOG_LEVEL_WARN = 2,
	OPLUS_APUIR_LOG_LEVEL_INFO = 3,
	OPLUS_APUIR_LOG_LEVEL_DEBUG = 4,
};

#define APUIR_DS_READ_LENGTH 75
#define APUIR_BUF_STR_LEN (APUIR_DS_READ_LENGTH * 3 + 1)
#define APUIR_DS_READ_LENGTH_NVT 75
#define APUIR_BUF_STR_LEN_NVT (APUIR_DS_READ_LENGTH_NVT * 3 + 1)
#define APUIR_DS_READ_ROW_NVT 1
#define APUIR_DS_READ_ROW_ILI 8
#define APUIR_DIM_READ_ROW_ILI 3
#define APUIR_DS_READ_LENGTH_ILI 30
#define APUIR_BUF_STR_LEN_ILI (APUIR_DS_READ_LENGTH_ILI * 3 + 1)
#define APUIR_DIM_READ_LENGTH_ILI 1
#define APUIR_DIM_BUF_STR_LEN_ILI (APUIR_DIM_READ_LENGTH_ILI * 3 + 1)
#define APUIR_OFF_FRAME_COUNT 10
#define APUIR_OFF_FRAME_COUNT_NVT 2
#define APUIR_OFF_FRAME_COUNT_ILI 10
#define APUIR_EXCHANGE_UPBAND 3
#define APUIR_EXCHANGE_LESSBAND 4
u8 apuirregs_loading[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_loading_mode1[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_loading_mode2[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_loading_mode3[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_dim_ili[APUIR_DIM_READ_LENGTH_ILI] = {0};

unsigned int oplus_apuir_log_level = OPLUS_APUIR_LOG_LEVEL_INFO;
EXPORT_SYMBOL(oplus_apuir_log_level);
unsigned int oplus_apuir_display_id = 0;
EXPORT_SYMBOL(oplus_apuir_display_id);
uint32_t m_apuirdim_ds = 0;
bool m_apuirdim_ds_update = false;
struct workqueue_struct *apuir_setcmd_wq;
static struct work_struct apuir_setcmd_work;
static enum dsi_cmd_id mAPuirType = DSI_CMD_APUIR_ON;
int off_framecount = 0;
static int first_loading = 0;
/*ili setting*/
int ili_nit_index_count = 4;
u32 m_ili_nit_index_list[4] = {0, 2, 3, 4};
int ili_dimvalue_count = 3;
u32 m_ili_dimvalue[3] = {0x00, 0x12, 0x14};
/*nvt setting*/
int nvt_upnit_index_count = 4;
u32 m_nvt_upnit_index_list[4] = {24, 32, 31, 33};
int nvt_lessnit_index_count = 4;
u32 m_nvt_lessnit_index_list[4] = {21, 24, 23, 25};
int nvt_modeset_count = 3;
u32 m_nvt_modeset_list[3] = {73, 0x11, 0x17};
int nvt_apl_lhset_count = 6;
u32 m_nvt_apl_lhset_list[6] = {60, 61, 0xFF, 0xFF, 0x3F, 0x33};
/*common setting*/
u32 m_ds1x0_real = 2988, m_ds1x3_real = 3367, m_ds2x0_real = 4095;
u32 m_ds21x0_real = 3635, m_ds21x3_real = 4095;
/*up uir on band nit param*/
int m_upnit_ds_list_count = 3;
u32 m_upnit_ds_list[3] = {2988, 3320, 4095};
/*less uir on band nit param*/
int m_lessnit_ds_list_count = 2;
u32 m_lessnit_ds_list[2] = {3635, 4095};
bool is_nvt_ic = false;
bool is_ili_ic = false;
int apuir_enable = 0;
u32 aplratio = 0, oprratio = 0;
EXPORT_SYMBOL(apuir_enable);
bool apuir_state = false;
EXPORT_SYMBOL(apuir_state);
int oplus_seed_mode = EXPERT;
EXPORT_SYMBOL(oplus_seed_mode);
bool m_ret = false;
u32 m_last_dim = 0;
u32 m_last_aplds = 0;
u32 aplds_before = 0;
enum dsi_cmd_id m_last_type = DSI_CMD_APUIR_OFF;
void oplus_set_seed_mode(int mode) {
	APUIR_INFO("oplus_set_seed_mode mode=%d\n", mode);
	oplus_seed_mode = mode;
}

void oplus_set_apuir_ictype(int ictype) {
	APUIR_INFO("oplus_set_apuir_ictype ictype=%d\n", ictype);
	switch (ictype) {
	case 1:
		is_ili_ic = true;
		is_nvt_ic = false;
		apuir_enable = 1;
		break;
	case 2:
		is_nvt_ic = true;
		is_ili_ic = false;
		apuir_enable = 1;
		break;
	default:
		is_nvt_ic = false;
		is_ili_ic = false;
		apuir_enable = 0;
		break;
	}
}
int oplus_get_apuir_enable(void) {
	if (is_nvt_ic || is_ili_ic) {
		apuir_enable = 1;
	} else {
		apuir_enable = 0;
	}
	return apuir_enable;
}
static void oplus_apuir_cmd_cmdq_cb(struct cmdq_cb_data data)
{
	struct mtk_cmdq_cb_data *cb_data = data.data;
	APUIR_DEBUG("%s\n", __func__);
	OPLUS_APUIR_TRACE_INT("oplus_ap_uir|%d", 1);
	OPLUS_APUIR_TRACE_INT("oplus_ap_uir|%d", 0);
	cmdq_pkt_destroy(cb_data->cmdq_handle);
	kfree(cb_data);
}
static void oplus_apuir_send_cmd(struct drm_crtc *crtc, enum dsi_cmd_id cmd_set_id) {
	bool is_frame_mode;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *cmdq_handle;
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_cmdq_cb_data *cb_data;
	struct mtk_dsi *dsi = container_of(comp, struct mtk_dsi, ddp_comp);
	is_frame_mode = mtk_crtc_is_frame_trigger_mode(&mtk_crtc->base);
	if (is_frame_mode) {
		mtk_drm_idlemgr_kick(__func__, crtc, 0);
		mtk_crtc_pkt_create(&cmdq_handle, &mtk_crtc->base,
						mtk_crtc->gce_obj.client[CLIENT_CFG]);

		if (mtk_crtc_with_sub_path(crtc, mtk_crtc->ddp_mode))
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_SECOND_PATH, 0);
		else
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_FIRST_PATH, 0);

		cmdq_pkt_clear_event(cmdq_handle,
				 	mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);
		cmdq_pkt_wfe(cmdq_handle,
		 			mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);

		oplus_dsi_panel_send_cmd(dsi, cmd_set_id, cmdq_handle, DSI_CMD_FUNC_DEFAULT);

		cmdq_pkt_set_event(cmdq_handle,
					mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		cmdq_pkt_set_event(cmdq_handle,
			mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);

		cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
		if (cb_data) {
			cb_data->cmdq_handle = cmdq_handle;
			/* indicate minfps cmd */
			cb_data->misc = 7;
			cmdq_pkt_flush_threaded(cmdq_handle, oplus_apuir_cmd_cmdq_cb, cb_data);
		} else {
			cmdq_pkt_flush(cmdq_handle);
			cmdq_pkt_destroy(cmdq_handle);
			return;
		}
	}
}

bool oplus_apuir_get_uir_state(void)
{
	uint32_t aplds = m_apuirdim_ds & 0xfff;
	if ((m_apuirdim_ds != 0 && aplds == 0
		&& ((off_framecount == APUIR_OFF_FRAME_COUNT_NVT && is_nvt_ic) || (off_framecount == APUIR_OFF_FRAME_COUNT_ILI && is_ili_ic)))
		|| m_apuirdim_ds == 0) {
		APUIR_INFO("oplus_apuir_get_uir_state return false\n");
		return false;
	} else {
		APUIR_INFO("oplus_apuir_get_uir_state return true\n");
		return true;
	}
}

void oplus_apuir_set_dsprop(void *drm_crtc, int prop_id, unsigned int propval) {
	APUIR_DEBUG("start\n");
	if (!oplus_get_apuir_enable()) {
		return;
	}
	uint32_t aplds = m_apuirdim_ds & 0xfff;
	switch (prop_id) {
	case CRTC_PROP_AP_UIR_DS:
		if (propval != m_apuirdim_ds || (off_framecount > 0 && off_framecount < APUIR_OFF_FRAME_COUNT)) {
			m_apuirdim_ds = propval;
			if (m_apuirdim_ds != 0) {
				m_apuirdim_ds_update = true;
			}
			APUIR_DEBUG("oplus_apuir_set_dsprop m_apuirdim_ds = %d %x m_apuirdim_ds_update = %d off_framecount %d \n",
				m_apuirdim_ds, m_apuirdim_ds, m_apuirdim_ds_update, off_framecount);
		}
		break;
	default:
		break;
	}
	APUIR_DEBUG("end\n");
}

void oplus_apuir_set_upnit_ds_list(int count, u32* list) {
	m_upnit_ds_list_count = count;
	if (m_upnit_ds_list_count != 3 || !list) {
		APUIR_ERR("oplus_apuir_set_upnit_ds_list %d != 3 or list is null\n", m_upnit_ds_list_count);
		return;
	}
	m_upnit_ds_list[0] = list[0];
	m_upnit_ds_list[1] = list[1];
	m_upnit_ds_list[2] = list[2];
	m_ds1x0_real = m_upnit_ds_list[0];
	m_ds1x3_real = m_upnit_ds_list[1];
	m_ds2x0_real = m_upnit_ds_list[2];
	APUIR_INFO("oplus_apuir_set_upnit_ds_list %d %d %d\n", m_upnit_ds_list[0], m_upnit_ds_list[1], m_upnit_ds_list[2]);
}

void oplus_apuir_set_lessnit_ds_list(int count, u32* list) {
	m_lessnit_ds_list_count  = count;
	if (m_lessnit_ds_list_count != 2 || !list) {
		APUIR_ERR("oplus_apuir_set_lessnit_ds_list %d != 2 or list is null\n", m_lessnit_ds_list_count);
		return;
	}
	m_lessnit_ds_list[0] = list[0];
	m_lessnit_ds_list[1] = list[1];
	m_ds21x0_real = m_lessnit_ds_list[0];
	m_ds21x3_real = m_lessnit_ds_list[1];
	APUIR_INFO("oplus_apuir_set_lessnit_ds_list %d %d\n", m_lessnit_ds_list[0], m_lessnit_ds_list[1]);
}

void apuir_set_ili_nit_index_list(int count, u32* list) {
	ili_nit_index_count = count;
	if (ili_nit_index_count != 4 || !list) {
		APUIR_ERR("apuir_set_ili_nit_index_list %d != 4 or list is null\n", ili_nit_index_count);
		return;
	}
	m_ili_nit_index_list[0] = list[0];
	m_ili_nit_index_list[1] = list[1];
	m_ili_nit_index_list[2] = list[2];
	m_ili_nit_index_list[3] = list[3];
	APUIR_INFO("apuir_set_ili_nit_index_list %d %d %d %d\n",
			m_ili_nit_index_list[0], m_ili_nit_index_list[1],
			m_ili_nit_index_list[2], m_ili_nit_index_list[3]);
}

void apuir_set_nvt_upnit_index_list(int count, u32* list) {
	nvt_upnit_index_count = count;
	if (nvt_upnit_index_count != 4 || !list) {
		APUIR_ERR("apuir_set_mvt_upnit_index_list %d != 4 or list is null\n", nvt_upnit_index_count);
		return;
	}
	m_nvt_upnit_index_list[0] = list[0];
	m_nvt_upnit_index_list[1] = list[1];
	m_nvt_upnit_index_list[2] = list[2];
	m_nvt_upnit_index_list[3] = list[3];
	APUIR_INFO("apuir_set_ili_nit_index_list %d %d %d %d\n",
		m_nvt_upnit_index_list[0], m_nvt_upnit_index_list[1],
		m_nvt_upnit_index_list[2], m_nvt_upnit_index_list[3]);
}

void apuir_set_nvt_lessnit_index_list(int count, u32* list) {
	nvt_lessnit_index_count = count;
	if (nvt_lessnit_index_count != 4 || !list) {
		APUIR_ERR("apuir_set_nvt_lessnit_index_list %d != 4 or list is null\n", nvt_lessnit_index_count);
		return;
	}
	m_nvt_lessnit_index_list[0] = list[0];
	m_nvt_lessnit_index_list[1] = list[1];
	m_nvt_lessnit_index_list[2] = list[2];
	m_nvt_lessnit_index_list[3] = list[3];
	APUIR_INFO("apuir_set_nvt_lessnit_index_list %d %d %d %d\n",
			m_nvt_lessnit_index_list[0], m_nvt_lessnit_index_list[1],
			m_nvt_lessnit_index_list[2], m_nvt_lessnit_index_list[3]);
}

void apuir_set_nvt_modeset_list(int count, u32* list) {
	nvt_modeset_count = count;
	if (nvt_modeset_count != 3 || !list) {
		APUIR_ERR("apuir_set_nvt_modeset_list %d != 3 or list is null\n", nvt_modeset_count);
		return;
	}
	m_nvt_modeset_list[0] = list[0];
	m_nvt_modeset_list[1] = list[1];
	m_nvt_modeset_list[2] = list[2];
	APUIR_INFO("apuir_set_nvt_modeset_list %d %d %x %d %x\n",
			m_nvt_modeset_list[0], m_nvt_modeset_list[1], m_nvt_modeset_list[1], m_nvt_modeset_list[2], m_nvt_modeset_list[2]);
}

void apuir_set_nvt_apl_lhset_list(int count, u32* list) {
	nvt_apl_lhset_count = count;
	if (nvt_apl_lhset_count != 6 || !list) {
		APUIR_ERR("apuir_set_nvt_apl_lhset_list %d != 6 or list is null\n", nvt_apl_lhset_count);
		return;
	}
	m_nvt_apl_lhset_list[0] = list[0];
	m_nvt_apl_lhset_list[1] = list[1];
	m_nvt_apl_lhset_list[2] = list[2];
	m_nvt_apl_lhset_list[3] = list[3];
	m_nvt_apl_lhset_list[4] = list[4];
	m_nvt_apl_lhset_list[5] = list[5];
	APUIR_INFO("apuir_set_nvt_apl_lhset_list %d %d %d %x %d %x %d %x %d %x\n",
			m_nvt_apl_lhset_list[0], m_nvt_apl_lhset_list[1], m_nvt_apl_lhset_list[2], m_nvt_apl_lhset_list[2],
			m_nvt_apl_lhset_list[3], m_nvt_apl_lhset_list[3], m_nvt_apl_lhset_list[4], m_nvt_apl_lhset_list[4],
			m_nvt_apl_lhset_list[5], m_nvt_apl_lhset_list[5]);
}

static void roundint(u32* ds, u32* ratio) {
	u32 temp = 0, temp1 = 0, temp2 = 0;
	temp = *ratio * (m_ds2x0_real - m_ds1x0_real);
	temp1 = temp / 100000 * 10;
	temp2 = temp / 10000;
	if (temp2 > temp1) {
		temp2 = temp2 + 10;
	}
	*ds = temp2 / 10 + m_ds1x0_real;
}

#define FIXED_SHIFT   16
#define FIXED_ONE     (1 << FIXED_SHIFT)
#define BASE_RATIO    100000
#define GAMMA_LUT_SIZE 512
#define LUT_SCALE 511

static const u32 gamma_lut_512[GAMMA_LUT_SIZE] = {
	0, 3849, 5275, 6342, 7228, 8000, 8691, 9322, 9905, 10450, 10963, 11448, 11910, 12351, 12775, 13182, 13574, 13953, 14320, 14677,
	15023, 15360, 15688, 16008, 16321, 16627, 16926, 17219, 17506, 17787, 18063, 18335, 18601, 18863, 19121, 19374, 19624, 19870,
	20112, 20351, 20587, 20819, 21048, 21275, 21498, 21719, 21937, 22153, 22366, 22576, 22784, 22990, 23194, 23396, 23596, 23793,
	23989, 24183, 24375, 24565, 24753, 24940, 25125, 25308, 25490, 25670, 25849, 26026, 26202, 26377, 26550, 26721, 26892, 27061,
	27229, 27396, 27561, 27725, 27888, 28050, 28211, 28371, 28530, 28687, 28844, 28999, 29154, 29308, 29460, 29612, 29763, 29913,
	30061, 30210, 30357, 30503, 30649, 30793, 30937, 31080, 31223, 31364, 31505, 31645, 31784, 31923, 32061, 32198, 32334, 32470,
	32605, 32739, 32873, 33006, 33139, 33271, 33402, 33532, 33662, 33792, 33920, 34049, 34176, 34303, 34430, 34556, 34681, 34806,
	34930, 35054, 35177, 35300, 35422, 35544, 35665, 35786, 35906, 36026, 36145, 36264, 36382, 36500, 36618, 36735, 36851, 36967,
	37083, 37198, 37313, 37428, 37542, 37655, 37768, 37881, 37993, 38105, 38217, 38328, 38439, 38549, 38659, 38769, 38878, 38987,
	39095, 39204, 39311, 39419, 39526, 39633, 39739, 39845, 39951, 40057, 40162, 40266, 40371, 40475, 40579, 40682, 40785, 40888,
	40991, 41093, 41195, 41296, 41398, 41499, 41599, 41700, 41800, 41900, 41999, 42099, 42198, 42296, 42395, 42493, 42591, 42689,
	42786, 42883, 42980, 43077, 43173, 43269, 43365, 43460, 43556, 43651, 43746, 43840, 43934, 44028, 44122, 44216, 44309, 44402,
	44495, 44588, 44680, 44773, 44865, 44956, 45048, 45139, 45230, 45321, 45412, 45502, 45592, 45682, 45772, 45862, 45951, 46040,
	46129, 46218, 46306, 46395, 46483, 46571, 46659, 46746, 46834, 46921, 47008, 47094, 47181, 47267, 47354, 47440, 47525, 47611,
	47696, 47782, 47867, 47952, 48036, 48121, 48205, 48289, 48373, 48457, 48541, 48624, 48708, 48791, 48874, 48957, 49039, 49122,
	49204, 49286, 49368, 49450, 49532, 49613, 49695, 49776, 49857, 49938, 50018, 50099, 50179, 50259, 50340, 50420, 50499, 50579,
	50658, 50738, 50817, 50896, 50975, 51054, 51132, 51211, 51289, 51367, 51445, 51523, 51601, 51678, 51756, 51833, 51910, 51987,
	52064, 52141, 52218, 52294, 52370, 52447, 52523, 52599, 52675, 52750, 52826, 52901, 52977, 53052, 53127, 53202, 53277, 53351,
	53426, 53500, 53575, 53649, 53723, 53797, 53871, 53944, 54018, 54091, 54165, 54238, 54311, 54384, 54457, 54529, 54602, 54675,
	54747, 54819, 54891, 54964, 55035, 55107, 55179, 55251, 55322, 55394, 55465, 55536, 55607, 55678, 55749, 55820, 55890, 55961,
	56031, 56101, 56172, 56242, 56312, 56382, 56451, 56521, 56591, 56660, 56729, 56799, 56868, 56937, 57006, 57075, 57143, 57212,
	57281, 57349, 57418, 57486, 57554, 57622, 57690, 57758, 57826, 57893, 57961, 58029, 58096, 58163, 58231, 58298, 58365, 58432,
	58498, 58565, 58632, 58698, 58765, 58831, 58898, 58964, 59030, 59096, 59162, 59228, 59294, 59359, 59425, 59491, 59556, 59621,
	59687, 59752, 59817, 59882, 59947, 60012, 60076, 60141, 60206, 60270, 60334, 60399, 60463, 60527, 60591, 60655, 60719, 60783,
	60847, 60911, 60974, 61038, 61101, 61165, 61228, 61291, 61354, 61417, 61480, 61543, 61606, 61669, 61731, 61794, 61856, 61919,
	61981, 62044, 62106, 62168, 62230, 62292, 62354, 62416, 62478, 62539, 62601, 62662, 62724, 62785, 62847, 62908, 62969, 63030,
	63091, 63152, 63213, 63274, 63335, 63396, 63456, 63517, 63577, 63638, 63698, 63758, 63818, 63879, 63939, 63999, 64059, 64119,
	64178, 64238, 64298, 64357, 64417, 64476, 64536, 64595, 64654, 64714, 64773, 64832, 64891, 64950, 65009, 65068, 65126, 65185,
	65244, 65302, 65361, 65419, 65478, 65536
};
static u32 calculate_ds_medium_precision(u32 maxds, u32 ratio, u32 scale_factor) {
	u32 numerator = BASE_RATIO + ratio;
	int64_t temp = (uint64_t)numerator << FIXED_SHIFT;
	int64_t frac = temp / scale_factor;

	APUIR_DEBUG("calculate_ds_medium_precision: maxds %d ratio=%d, scale_factor %d, numerator=%d, frac=%d", maxds, ratio, scale_factor, numerator, (u32)frac);

	if (frac < 0) {
		frac = 0;
	} else if (frac > FIXED_ONE) {
		frac = FIXED_ONE;
	}

	u32 index = (frac * (GAMMA_LUT_SIZE - 1)) >> FIXED_SHIFT;
	if (index >= GAMMA_LUT_SIZE) {
		index = GAMMA_LUT_SIZE - 1;
	}

	u32 gamma_value = gamma_lut_512[index];
	APUIR_DEBUG("calculate_ds_medium_precision: index=%d, gamma_value=%d", index, gamma_value);

	int64_t result = ((uint64_t)maxds * gamma_value + (1 << (FIXED_SHIFT-1))) >> FIXED_SHIFT;
	APUIR_DEBUG("calculate_ds_medium_precision: maxds=%d, result=%d", maxds, (u32)result);

	return (result > 0xFFFF) ? 0xFFFF : (u32)result;
}

static void transfer_ds(u32* aplds, u32* oprds, u32* aplratio, u32* oprratio) {
	u32 DS1X0_predic = 2801;
	u32 DS2X0_predic = 3839;
	int tempa = *aplds, tempo = *oprds;
	u32 scale_factor = 200000;

	aplds_before = *aplds;
	if (is_ili_ic) {
		if (*oprds == 0) {
			*aplds = DS2X0_predic;
		}
	}
	tempa = *aplds;

	if (*aplds != 0) {
		*aplratio = (*aplds - DS1X0_predic) * 100000 / (DS2X0_predic - DS1X0_predic);
		if (is_ili_ic) {
			roundint(aplds, aplratio);
		}
	}
	if (*oprds != 0) {
		*oprratio = (*oprds - DS1X0_predic) * 100000 / (DS2X0_predic - DS1X0_predic);
		if (is_ili_ic) {
			roundint(oprds, oprratio);
		}
	}
	if (is_nvt_ic) {
		if (*aplds != 0) {
			*aplds = calculate_ds_medium_precision(m_ds2x0_real, *aplratio, scale_factor);
		}
		if (*oprds != 0) {
			*oprds = calculate_ds_medium_precision(m_ds2x0_real, *oprratio, scale_factor);
		}
	}

	APUIR_INFO("apuirdriver aplratio %d aplds_before %d aplds %d->%d oprratio %d oprds %d->%d\n",
		*aplratio, aplds_before, tempa, *aplds, *oprratio, tempo, *oprds);
}

static void exchangeregs_nvt(int ds, u8* apuirregs, int index0, int index1, int index2, int index3) {
	int temp1 = 0, temp2 = 1, temp3 = 0, temp4 = 0;
	int dsh = 0, dsl = 0;

	dsh = (ds >> 8) & 0xFF;
	dsl = ds & 0xFF;

	temp1 = apuirregs[index0];
	temp2 = apuirregs[index1];
	temp3 = apuirregs[index2];
	temp4 = apuirregs[index3];

	temp1 = (temp1 & 0x0F) | (dsh << 4);
	temp2 = (temp2 & 0xF0) | (dsh & 0x0F);
	temp3 = dsl;
	temp4 = dsl;

	apuirregs[index0] = temp1;
	apuirregs[index1] = temp2;
	apuirregs[index2] = temp3;
	apuirregs[index3] = temp4;
	APUIR_DEBUG("exchangeregs_nvt apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x\n",
		index0, apuirregs[index0], index1, apuirregs[index1], index2, apuirregs[index2], index3, apuirregs[index3]);
}

static void oplus_apuir_nvt_set_cmd(u8* apuirregs, u32 ds2, u32 aplds, u32 oprds) {
	int rc = 0;
	unsigned int ds = m_apuirdim_ds;
	u32 modepose = m_nvt_modeset_list[0];
	u32 apl_lpose = m_nvt_apl_lhset_list[0], apl_hpose = m_nvt_apl_lhset_list[1];
	u32 aplmode = m_nvt_modeset_list[1], oprmode = m_nvt_modeset_list[2];
	int index0 = m_nvt_upnit_index_list[0], index1 = m_nvt_upnit_index_list[1], index2 = m_nvt_upnit_index_list[2], index3 = m_nvt_upnit_index_list[3];
	int index4 = m_nvt_lessnit_index_list[0], index5 = m_nvt_lessnit_index_list[1], index6 = m_nvt_lessnit_index_list[2], index7 = m_nvt_lessnit_index_list[3];
	int index = 0;

	/*exchange less uir on band nit ds*/
	if (aplds > 0 || (off_framecount >= 1 && off_framecount < APUIR_OFF_FRAME_COUNT_NVT)) {
		exchangeregs_nvt(ds2, apuirregs, index4, index5, index6, index7);
	}

	/*exchange up uir on band nit ds*/
	if (aplds == 0) {
		if (off_framecount >= 1 && off_framecount < APUIR_OFF_FRAME_COUNT_NVT) {
			/*mAPuirType = DSI_CMD_APUIR_MIDDLE_OFF;*/
			apuirregs[modepose] = oprmode;
			apuirregs[apl_lpose] = m_nvt_apl_lhset_list[2];
			apuirregs[apl_hpose] = m_nvt_apl_lhset_list[3];
		}
		if (mAPuirType != DSI_CMD_APUIR_OFF) {
			exchangeregs_nvt(oprds, apuirregs, index0, index1, index2, index3);
		}
	} else if (aplds > 0) {
		/*mAPuirType = DSI_CMD_APUIR_ON;*/
		apuirregs[modepose] = aplmode;
		apuirregs[apl_lpose] = m_nvt_apl_lhset_list[4];
		apuirregs[apl_hpose] = m_nvt_apl_lhset_list[5];
		exchangeregs_nvt(aplds, apuirregs, index0, index1, index2, index3);
	}

	APUIR_INFO("offcount %d DSI_CMD_APUIR_ON %d mAPuirType = %d oplus_seed_mode = %d regs len=%d\n",
			off_framecount, DSI_CMD_APUIR_ON, mAPuirType, oplus_seed_mode, APUIR_DS_READ_LENGTH_NVT);
	OPLUS_APUIR_TRACE_BEGIN("oplus_apuir_set_cmd_replace");
	oplus_panel_cmd_reg_replace_specific_row(mAPuirType, apuirregs_loading, APUIR_DS_READ_LENGTH_NVT, APUIR_DS_READ_ROW_NVT);
	OPLUS_APUIR_TRACE_END("oplus_apuir_set_cmd_replace");
}

static void exchangeregs_ili(int ds, u8* apuirregs, int count, u32 oprds) {
	int dsh = 0, dsl = 0;
	int temp = m_ili_nit_index_list[0], temp1 = 0;
	int dsbefore1 = 0, dsbefore2 = 0;
	bool ret1 = false, ret2 = false;

	dsh = (ds >> 8) & 0x03;
	dsl = ds & 0xFF;

	switch (count) {
	case APUIR_EXCHANGE_UPBAND:
		/*exchange upnit ds*/
		temp1 = m_ili_nit_index_list[1];
		dsbefore1 = ((apuirregs[temp] & 0x30) << 4) | apuirregs[temp1];
		if (dsbefore1 < ds) {
			apuirregs[temp] = (apuirregs[temp] & 0xCF) | (dsh << 4);
			apuirregs[temp1] = dsl;
			ret1 = true;
		}
		temp1 = m_ili_nit_index_list[2];
		dsbefore2 = ((apuirregs[temp] & 0x0C) << 6) | apuirregs[temp1];
		if (dsbefore2 < ds) {
			apuirregs[temp] = (apuirregs[temp] & 0xF3) | (dsh << 2);
			apuirregs[temp1] = dsl;
			ret2 = true;
		}
		m_ret |= ret1;
		m_ret |= ret2;
		if (oprds != 0 && m_ret == false) {
			mAPuirType = DSI_CMD_APUIR_OFF;
		}
		APUIR_DEBUG("exchangeregs_ili upnit ret1 %d ret2 %d m_ret %d dsbefore1 = %d %x dsbefore2 = %d %x ds %d %x mAPuirType = %d\n",
			ret1, ret2, m_ret, dsbefore1, dsbefore1, dsbefore2, dsbefore2, ds, ds, mAPuirType);
		break;
	case APUIR_EXCHANGE_LESSBAND:
		/*exchange lessnit ds*/
		temp1 = m_ili_nit_index_list[3];
		dsbefore1 = ((apuirregs[temp] & 0x03) << 8) | apuirregs[temp1];
		if (dsbefore1 < ds) {
			apuirregs[temp] = (apuirregs[temp] & 0xFC) | dsh;
			apuirregs[temp1] = dsl;
			ret1 = true;
		}

		m_ret = ret1;
		APUIR_DEBUG("exchangeregs_ili lessnit ret1 %d m_ret %d dsbefore1 = %d %x ds %d %x\n", ret1, m_ret, dsbefore1, dsbefore1, ds, ds);
		break;
	default:
		break;
	}
}

static void oplus_apuir_ili_set_cmd(u8* apuirregs, u32 ds2, u32 aplds, u32 oprds) {
	int rc = 0;
	unsigned int ds = m_apuirdim_ds;
	int index = 0;
	u32 DS2X0_predic = 3839;

	if (aplds > 0 && mAPuirType == DSI_CMD_APUIR_ON) {
		exchangeregs_ili(ds2, apuirregs, APUIR_EXCHANGE_LESSBAND, oprds);
		exchangeregs_ili(aplds, apuirregs, APUIR_EXCHANGE_UPBAND, oprds);
		off_framecount = 0;
	} else {
		m_ret = false;
	}

	APUIR_INFO("offcount %d DSI_CMD_APUIR_ON %d mAPuirType = %d m_last_aplds %d aplds = %d oplus_seed_mode = %d regs len=%d\n",
			off_framecount, DSI_CMD_APUIR_ON, mAPuirType, m_last_aplds, aplds, oplus_seed_mode, APUIR_DS_READ_LENGTH_ILI);

	OPLUS_APUIR_TRACE_BEGIN("oplus_apuir_set_cmd_replace");
	if (aplds_before < DS2X0_predic && oprds == 0 && mAPuirType == DSI_CMD_APUIR_ON) {
		apuirregs_dim_ili[0] = m_ili_dimvalue[1];
	} else if (mAPuirType == DSI_CMD_APUIR_OFF && off_framecount != APUIR_OFF_FRAME_COUNT_ILI + 1) {
		apuirregs_dim_ili[0] = m_ili_dimvalue[2];
	} else {
		apuirregs_dim_ili[0] = m_ili_dimvalue[0];
	}
	oplus_panel_cmd_reg_replace_specific_row(mAPuirType, apuirregs_dim_ili, APUIR_DIM_READ_LENGTH_ILI, APUIR_DIM_READ_ROW_ILI);
	oplus_panel_cmd_reg_replace_specific_row(mAPuirType, apuirregs_loading, APUIR_DS_READ_LENGTH_ILI, APUIR_DS_READ_ROW_ILI);
	OPLUS_APUIR_TRACE_END("oplus_apuir_set_cmd_replace");
}

void oplus_apuir_set_cmd(void *mtk_drm_crtc)
{
	struct mtk_drm_crtc *mtk_crtc = mtk_drm_crtc;
	struct drm_crtc *crtc;
	u8* apuirregs = apuirregs_loading;
	u32 aplds = m_apuirdim_ds & 0xfff, oprds = m_apuirdim_ds >> 12;
	int row = 0;
	int length = 0;
	/* upnit ds */
	u32 ds_min = m_ds1x0_real, ds_max = m_ds2x0_real;
	/* lessnit ds2 */
	u32 ds2_min = m_ds21x0_real, ds2_max = m_ds21x3_real;
	u32 ratio = 0;
	u32 ds2 = 0;
	int ret = 0;
	u32 scale_factor = 0;

	if (!oplus_get_apuir_enable() || m_apuirdim_ds == 0 || (!m_apuirdim_ds_update && (off_framecount == 0 || off_framecount > APUIR_OFF_FRAME_COUNT))) {
		return;
	}

	if (!mtk_crtc) {
		APUIR_ERR("Invalid mtk_drm_crtc param\n");
		return;
	}

	crtc = &(mtk_crtc->base);
	if (!crtc) {
		APUIR_ERR("find drm_crtc fail\n");
		return;
	}
	OPLUS_APUIR_TRACE_BEGIN("oplus_apuir_set_cmd");
	if (is_nvt_ic || is_ili_ic) {
		enum dsi_cmd_id loading1type = DSI_CMD_SET_UIR_SEED_EXPERT;
		enum dsi_cmd_id loading2type = DSI_CMD_SET_UIR_SEED_NATURAL;
		enum dsi_cmd_id loading3type = DSI_CMD_SET_UIR_SEED_VIVID;

		m_apuirdim_ds_update = false;
		transfer_ds(&aplds, &oprds, &aplratio, &oprratio);
		if (is_nvt_ic) {
			row = APUIR_DS_READ_ROW_NVT;
			length = APUIR_DS_READ_LENGTH_NVT;
		}
		if (is_ili_ic) {
			row = APUIR_DS_READ_ROW_ILI;
			length = APUIR_DS_READ_LENGTH_ILI;
		}
		if (!first_loading) {
			APUIR_INFO("loading apuir cmds\n");
			ret = oplus_panel_cmd_reg_read_specific_row(loading1type, apuirregs_loading_mode1, length, row);
			if (ret <0) {
				APUIR_ERR("loading apuirregs_loading_mode1 error\n");
				return;
			}
			ret = oplus_panel_cmd_reg_read_specific_row(loading2type, apuirregs_loading_mode2, length, row);
			if (ret <0) {
				APUIR_ERR("loading apuirregs_loading_mode2 error\n");
				return;
			}
			ret = oplus_panel_cmd_reg_read_specific_row(loading3type, apuirregs_loading_mode3, length, row);
			if (ret <0) {
				APUIR_ERR("loading apuirregs_loading_mode3 error\n");
				return;
			}
			first_loading = 1;
		}

		switch (oplus_seed_mode) {
		case EXPERT:
			memcpy(apuirregs, apuirregs_loading_mode1, sizeof(apuirregs_loading_mode1));
			break;
		case NATURAL:
			memcpy(apuirregs, apuirregs_loading_mode2, sizeof(apuirregs_loading_mode2));
			break;
		case VIVID:
			memcpy(apuirregs, apuirregs_loading_mode3, sizeof(apuirregs_loading_mode3));
			break;
		default:
			memcpy(apuirregs, apuirregs_loading_mode1, sizeof(apuirregs_loading_mode1));
			break;
		}

		if (aplds > 0) {
			if (aplds > m_ds1x3_real) {
				ds2 = ds2_max;
				APUIR_DEBUG("apuirdriver aplds %d > %d ds2 = %d 0x%x\n", aplds, m_ds1x3_real, ds2, ds2);
			} else {
				ratio = aplratio;
				scale_factor = 130000;
				ds2 = calculate_ds_medium_precision(m_ds21x3_real, aplratio, scale_factor);
				if (ds2 > ds2_max) {
					int temp = ds2;
					ds2 = ds2_max;
					APUIR_DEBUG("apuirdriver aplds %d > 0 ds2 = %d 0x%x ratio = %d / 100000 temp = %d\n", aplds, ds2, ds2, ratio, temp);
				} else {
					APUIR_DEBUG("apuirdriver aplds %d > 0 ds2 = %d 0x%x ratio = %d / 100000\n", aplds, ds2, ds2, ratio);
				}
			}
			mAPuirType = DSI_CMD_APUIR_ON;
		} else if (is_nvt_ic && off_framecount < APUIR_OFF_FRAME_COUNT_NVT - 1) {
			if (oprds > m_ds1x3_real) {
				ds2 = ds2_max;
				APUIR_DEBUG("apuirdriver oprds %d aplds == 0 oprds > %d ds2 = %d 0x%x\n", oprds, m_ds1x3_real, ds2, ds2);
			} else {
				ratio = oprratio;
				scale_factor = 130000;
				ds2 = calculate_ds_medium_precision(m_ds21x3_real, aplratio, scale_factor);
				if (ds2 > ds2_max) {
					int temp = ds2;
					ds2 = ds2_max;
					APUIR_DEBUG("apuirdriver oprds %d aplds == 0 ds2 = %d 0x%x ratio = %d / 100000 temp = %d\n", oprds, ds2, ds2, ratio, temp);
				} else {
					APUIR_DEBUG("apuirdriver oprds %d aplds == 0 ds2 = %d 0x%x ratio = %d / 100000\n", oprds, ds2, ds2, ratio);
				}
			}
		}
	}

	if (is_nvt_ic) {
		if (aplds == 0) {
			off_framecount++;
			if (off_framecount >= 1 && off_framecount < APUIR_OFF_FRAME_COUNT_NVT) {
				mAPuirType = DSI_CMD_APUIR_MIDDLE_OFF;
			} else if (off_framecount == APUIR_OFF_FRAME_COUNT_NVT) {
				mAPuirType = DSI_CMD_APUIR_OFF;
			}
		} else {
			off_framecount = 0;
			mAPuirType = DSI_CMD_APUIR_ON;
		}
		oplus_apuir_nvt_set_cmd(apuirregs, ds2, aplds, oprds);
	}

	if (is_ili_ic) {
		if (aplds == 0) {
			off_framecount++;
			mAPuirType = DSI_CMD_APUIR_OFF;
		} else {
			off_framecount = 0;
			mAPuirType = DSI_CMD_APUIR_ON;
			if (oprds != 0) {
				mAPuirType = DSI_CMD_APUIR_OFF;
			}
		}
		oplus_apuir_ili_set_cmd(apuirregs, ds2, aplds, oprds);
	}

	if (is_nvt_ic || is_ili_ic) {
		if (is_ili_ic && off_framecount != APUIR_OFF_FRAME_COUNT_ILI + 1
			&& ((m_last_type == mAPuirType && m_last_dim == apuirregs_dim_ili[0] && m_last_aplds == aplds)
				|| (mAPuirType == DSI_CMD_APUIR_OFF && m_last_type == mAPuirType))) {
			m_last_type = mAPuirType;
			m_last_dim = apuirregs_dim_ili[0];
			m_last_aplds = aplds;
			APUIR_DEBUG("is_ili_ic aplratio %d type %d %d dim %d %d ret %d is same return\n",
				aplratio, mAPuirType, m_last_type, apuirregs_dim_ili[0], m_last_dim, m_ret);
			return;
		}

		if (is_nvt_ic && off_framecount > APUIR_OFF_FRAME_COUNT_NVT) {
			APUIR_DEBUG("is_nvt_ic && off_framecount > APUIR_OFF_FRAME_COUNT_NVT return\n");
			return;
		}
		OPLUS_APUIR_TRACE_BEGIN("oplus_apuir_send_cmd");
		oplus_apuir_send_cmd(crtc, mAPuirType);
		OPLUS_APUIR_TRACE_END("oplus_apuir_send_cmd");
		if (mAPuirType == DSI_CMD_APUIR_ON) {
			apuir_state = true;
		} else if (mAPuirType == DSI_CMD_APUIR_OFF) {
			apuir_state = false;
		}
		APUIR_DEBUG("type %d %d dim %d %d ret %d aplds %d %d off_framecount %d is diff set cmd apuir_state %d\n",
			mAPuirType, m_last_type, apuirregs_dim_ili[0], m_last_dim, m_ret, m_last_aplds, aplds, off_framecount, apuir_state);
		m_last_type = mAPuirType;
		m_last_dim = apuirregs_dim_ili[0];
		m_last_aplds = aplds;
	}
	OPLUS_APUIR_TRACE_END("oplus_apuir_set_cmd");
}

