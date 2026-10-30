/**
 ****************************************************************************************************
 * @file        app_camera.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       app_camera.c文件
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 N647开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "app_camera.h"
#include "app_config.h"
#include "app.h"
#include "stm32n6xx_hal.h"

static int app_camera_display_pipe_init(void);
static int app_camera_nn_pipe_init(void);

static void (*app_camera_display_pipe_vsync_user_cb)(void) = NULL;
static void (*app_camera_display_pipe_frame_user_cb)(void) = NULL;
static void (*app_camera_nn_pipe_vsync_user_cb)(void) = NULL;
static void (*app_camera_nn_pipe_frame_user_cb)(void) = NULL;

int app_camera_init(void (*display_pipe_vsync_cb)(void), void (*display_pipe_frame_cb)(void), void (*nn_pipe_vsync_cb)(void), void (*nn_pipe_frame_cb)(void))
{
    CMW_CameraInit_t cmw_camera_init = {0};

    cmw_camera_init.width = 0;
    cmw_camera_init.height = 0;
    cmw_camera_init.fps = 20;
    cmw_camera_init.pixel_format = 0;
    cmw_camera_init.anti_flicker = 0;
    cmw_camera_init.mirror_flip = CAMERA_MIRROR_FLIP;
    if (CMW_CAMERA_Init(&cmw_camera_init) != CMW_ERROR_NONE) return -1;

    if (app_camera_display_pipe_init() != 0 || app_camera_nn_pipe_init() != 0) return -1;

    if (display_pipe_vsync_cb != NULL)
    {
        app_camera_display_pipe_vsync_user_cb = display_pipe_vsync_cb;
    }

    if (display_pipe_frame_cb != NULL)
    {
        app_camera_display_pipe_frame_user_cb = display_pipe_frame_cb;
    }

    if (nn_pipe_vsync_cb != NULL)
    {
        app_camera_nn_pipe_vsync_user_cb = nn_pipe_vsync_cb;
    }

    if (nn_pipe_frame_cb != NULL)
    {
        app_camera_nn_pipe_frame_user_cb = nn_pipe_frame_cb;
    }
    return 0;
}

void app_camera_display_pipe_start(uint8_t *display_pipe_destination, uint32_t capture_mode)
{
    CMW_CAMERA_Start(DCMIPP_PIPE1, display_pipe_destination, capture_mode);
}

void app_camera_nn_pipe_start(uint8_t *nn_pipe_destination, uint32_t capture_mode)
{
    CMW_CAMERA_Start(DCMIPP_PIPE2, nn_pipe_destination, capture_mode);
}

void app_camera_isp_update(void)
{
    CMW_CAMERA_Run();
}

static int app_camera_display_pipe_init(void)
{
    CMW_DCMIPP_Conf_t cmw_dcmipp_conf = {0};
    uint32_t hw_pitch;

    cmw_dcmipp_conf.output_width = LCD_BG_WIDTH;
    cmw_dcmipp_conf.output_height = LCD_BG_HEIGHT;
    cmw_dcmipp_conf.output_format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    cmw_dcmipp_conf.output_bpp = 2;
    cmw_dcmipp_conf.enable_swap = 0;
    cmw_dcmipp_conf.enable_gamma_conversion = 0;
    cmw_dcmipp_conf.mode = CMW_Aspect_ratio_crop;
    return CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE1, &cmw_dcmipp_conf, &hw_pitch) == CMW_ERROR_NONE ? 0 : -1;
}

static int app_camera_nn_pipe_init(void)
{
    CMW_DCMIPP_Conf_t cmw_dcmipp_conf = {0};
    uint32_t hw_pitch;

    cmw_dcmipp_conf.output_width = NN_WIDTH;
    cmw_dcmipp_conf.output_height = NN_HEIGHT;
    cmw_dcmipp_conf.output_format = NN_FORMAT;
    cmw_dcmipp_conf.output_bpp = NN_BPP;
    cmw_dcmipp_conf.enable_swap = 1;
    cmw_dcmipp_conf.enable_gamma_conversion = 0;
    cmw_dcmipp_conf.mode = CMW_Aspect_ratio_crop;
    return CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE2, &cmw_dcmipp_conf, &hw_pitch) == CMW_ERROR_NONE ? 0 : -1;
}

HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
    RCC_PeriphCLKInitTypeDef rcc_periph_clk_init_struct = {0};
    (void)hdcmipp;

    rcc_periph_clk_init_struct.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP | RCC_PERIPHCLK_CSI;
    rcc_periph_clk_init_struct.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
    rcc_periph_clk_init_struct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
    rcc_periph_clk_init_struct.ICSelection[RCC_IC17].ClockDivider = 3;
    rcc_periph_clk_init_struct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    rcc_periph_clk_init_struct.ICSelection[RCC_IC18].ClockDivider = 40;
    if (HAL_RCCEx_PeriphCLKConfig(&rcc_periph_clk_init_struct) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

int CMW_CAMERA_PIPE_VsyncEventCallback(uint32_t pipe)
{
    if (pipe == DCMIPP_PIPE1)
    {
        if (app_camera_display_pipe_vsync_user_cb != NULL)
        {
            app_camera_display_pipe_vsync_user_cb();
        }
    }
    else if (pipe == DCMIPP_PIPE2)
    {
        if (app_camera_nn_pipe_vsync_user_cb != NULL)
        {
            app_camera_nn_pipe_vsync_user_cb();
        }
    }

    return 0;
}

int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
    if (pipe == DCMIPP_PIPE1)
    {
        if (app_camera_display_pipe_frame_user_cb != NULL)
        {
            app_camera_display_pipe_frame_user_cb();
        }
    }
    else if (pipe == DCMIPP_PIPE2)
    {
        if (app_camera_nn_pipe_frame_user_cb != NULL)
        {
            app_camera_nn_pipe_frame_user_cb();
        }
    }

    return 0;
}
