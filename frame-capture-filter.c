#include <math.h>
#include <stdio.h>
#include <shlobj.h>
#include <inttypes.h>
#include <util/platform.h>
#include <util/threading.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include <obs-module.h>
#include <libjpeg/jpeglib.h>

#define OVERWATCH_QUALITY                         90
#define OVERWATCH_DESIRED_WIDTH                   1920
#define OVERWATCH_DESIRED_HEIGHT                  1080
#define OVERWATCH_FRAMES_PER_FOLDER               15
#define OVERWATCH_MILLISECONDS_BETWEEN_FRAMES     2000

#define FORTNITE_QUALITY                          70
#define FORTNITE_DESIRED_WIDTH                    1280
#define FORTNITE_DESIRED_HEIGHT                   720
#define FORTNITE_FRAMES_PER_FOLDER                20
#define FORTNITE_MILLISECONDS_BETWEEN_FRAMES      1000

#define SETTING_GAME                              "game"
#define SETTING_OVERWATCH                         "overwatch"
#define SETTING_FORTNITE                          "fortnite"

#define TEXT_GAME                                 "Game"
#define TEXT_OVERWATCH                            "Overwatch"
#define TEXT_FORTNITE                             "Fortnite"
#define LONG_TEXT_OVERWATCH                       L"Overwatch"
#define LONG_TEXT_FORTNITE                        L"Fortnite"

OBS_DECLARE_MODULE()

struct frame_capture_filter_data {
  obs_source_t *source;

  wchar_t *save_path;
  wchar_t *current_folder;
  uint32_t frame_count;
  DWORD last_frame_at;
  uint32_t width;
  uint32_t height;
  gs_texrender_t* texrender;
  gs_stagesurf_t* stagesurface;
  uint8_t *frame_data;
  uint32_t frame_linesize;

  pthread_mutex_t write_mutex;
  pthread_t write_thread;
  os_sem_t *write_sem;
  os_event_t *stop_event;

  wchar_t *previous_game;
  wchar_t *game;
  uint32_t quality;
  uint32_t desired_width;
  uint32_t desired_height;
  uint32_t frames_per_folder;
  uint32_t milliseconds_between_frames;
};

static const char *frame_capture_filter_name(void *data)
{
  UNUSED_PARAMETER(data);
  return "Pursuit Frame Capture";
}

static void frame_capture_filter_update(void *data, obs_data_t *settings)
{
  struct frame_capture_filter_data *filter = data;
  const char *game = obs_data_get_string(settings, SETTING_GAME);

  pthread_mutex_lock(&filter->write_mutex);
  if (strcmp(game, SETTING_OVERWATCH) == 0) {
    filter->game = LONG_TEXT_OVERWATCH;
    filter->quality = OVERWATCH_QUALITY;
    filter->desired_width = OVERWATCH_DESIRED_WIDTH;
    filter->desired_height = OVERWATCH_DESIRED_HEIGHT;
    filter->frames_per_folder = OVERWATCH_FRAMES_PER_FOLDER;
    filter->milliseconds_between_frames = OVERWATCH_MILLISECONDS_BETWEEN_FRAMES;
  } else if (strcmp(game, SETTING_FORTNITE) == 0) {
    filter->game = LONG_TEXT_FORTNITE;
    filter->quality = FORTNITE_QUALITY;
    filter->desired_width = FORTNITE_DESIRED_WIDTH;
    filter->desired_height = FORTNITE_DESIRED_HEIGHT;
    filter->frames_per_folder = FORTNITE_FRAMES_PER_FOLDER;
    filter->milliseconds_between_frames = FORTNITE_MILLISECONDS_BETWEEN_FRAMES;
  }
  pthread_mutex_unlock(&filter->write_mutex);
}

static bool open_pursuit(obs_properties_t *pps, obs_property_t *prop, void *data)
{
  ShellExecute(NULL, "open", "https://pursuit.gg/obs", NULL, NULL, SW_SHOWNORMAL);
  return true;
}

static obs_properties_t *frame_capture_filter_properties(void *data)
{
  UNUSED_PARAMETER(data);

  obs_properties_t *props = obs_properties_create();

  obs_property_t *games = obs_properties_add_list(props, SETTING_GAME, TEXT_GAME, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(games, TEXT_OVERWATCH, SETTING_OVERWATCH);
  obs_property_list_add_string(games, TEXT_FORTNITE, SETTING_FORTNITE);
  obs_properties_add_button(props, "pursuit_website", "Pursuit.gg Plugin Instructions", open_pursuit);
  return props;
}

void frame_capture_filter_defaults(obs_data_t* settings) {
  obs_data_set_default_string(settings, SETTING_GAME, SETTING_OVERWATCH);
}

static void generate_folder(SYSTEMTIME systemtime, wchar_t *folder, wchar_t *game, wchar_t *save_path)
{
  wchar_t dirname[MAX_PATH];

  swprintf_s(folder, sizeof(wchar_t) * 18, L"%04d%02d%02d%02d%02d%02d%03d", systemtime.wYear, systemtime.wMonth, systemtime.wDay, systemtime.wHour, systemtime.wMinute, systemtime.wSecond, systemtime.wMilliseconds);
  wcscpy_s(dirname, sizeof(dirname), save_path);
  wcscat_s(dirname, sizeof(dirname), L"/");
  wcscat_s(dirname, sizeof(dirname), game);
  wcscat_s(dirname, sizeof(dirname), L"/");
  wcscat_s(dirname, sizeof(dirname), folder);
  _wmkdir(dirname);
}

static void generate_filename(SYSTEMTIME systemtime, wchar_t *fname, wchar_t *folder, wchar_t *game, wchar_t *save_path)
{
  wchar_t timestring[18];

  swprintf_s(timestring, sizeof(timestring), L"%04d%02d%02d%02d%02d%02d%03d", systemtime.wYear, systemtime.wMonth, systemtime.wDay, systemtime.wHour, systemtime.wMinute, systemtime.wSecond, systemtime.wMilliseconds);
  wcscpy_s(fname, sizeof(wchar_t) * MAX_PATH, save_path);
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, L"/");
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, game);
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, L"/");
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, folder);
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, L"/");
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, timestring);
  wcscat_s(fname, sizeof(wchar_t) * MAX_PATH, L".jpeg");
}

static void finish_folder(wchar_t *folder, wchar_t *game, wchar_t *save_path)
{
  wchar_t fname[MAX_PATH];

  if (folder) {
    wcscpy_s(fname, sizeof(fname), save_path);
    wcscat_s(fname, sizeof(fname), L"/");
    wcscat_s(fname, sizeof(fname), game);
    wcscat_s(fname, sizeof(fname), L"/");
    wcscat_s(fname, sizeof(fname), folder);
    wcscat_s(fname, sizeof(fname), L"/done");

    FILE *f = _wfopen(fname, L"wb");
    fclose(f);
  }
}

static void save_frame(uint8_t *raw_frame, uint32_t linesize, uint32_t width, uint32_t height, int quality, wchar_t *fname)
{
  FILE *f = _wfopen(fname, L"wb");

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, f);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, true);
  jpeg_start_compress(&cinfo, true);

  JSAMPROW row_ptr[1];
  uint8_t *row_buf = bzalloc(sizeof(uint8_t) * cinfo.image_width * 3);
  row_ptr[0] = &row_buf[0];

  while (cinfo.next_scanline < cinfo.image_height) {
    uint32_t offset = cinfo.next_scanline * linesize;
    for (uint32_t i = 0; i < cinfo.image_width; i++) {
      row_buf[i * 3] = raw_frame[offset + (i * 4)];
      row_buf[(i * 3) + 1] = raw_frame[offset + (i * 4) + 1];
      row_buf[(i * 3) + 2] = raw_frame[offset + (i * 4) + 2];
    }
    jpeg_write_scanlines(&cinfo, row_ptr, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  bfree(row_buf);
  fclose(f);
}

static void process_raw_frame(void *data)
{
  struct frame_capture_filter_data *filter = data;

  SYSTEMTIME systemtime;
  wchar_t fname[MAX_PATH];
  GetSystemTime(&systemtime);

  if (filter->current_folder == NULL || filter->frame_count >= filter->frames_per_folder || wcscmp(filter->previous_game, filter->game) != 0) {
    finish_folder(filter->current_folder, filter->previous_game, filter->save_path);
    wchar_t *folder = bzalloc(sizeof(wchar_t) * 18);
    generate_folder(systemtime, folder, filter->game, filter->save_path);
    bfree(filter->current_folder);
    filter->current_folder = folder;
    filter->frame_count = 0;
    filter->previous_game = filter->game;
  }
  generate_filename(systemtime, fname, filter->current_folder, filter->game, filter->save_path);
  save_frame(filter->frame_data, filter->frame_linesize, filter->width, filter->height, filter->quality, fname);
  filter->frame_count = filter->frame_count + 1;
}

static void *write_thread(void *data)
{
  struct frame_capture_filter_data *filter = data;

  while (os_sem_wait(filter->write_sem) == 0) {
    if (os_event_try(filter->stop_event) == 0)
      break;
    pthread_mutex_lock(&filter->write_mutex);
    process_raw_frame(filter);
    pthread_mutex_unlock(&filter->write_mutex);
  }
  return NULL;
}

void frame_capture_filter_offscreen_render(void *data, uint32_t cx, uint32_t cy)
{
  UNUSED_PARAMETER(cx);
  UNUSED_PARAMETER(cy);
  struct frame_capture_filter_data *filter = data;

  DWORD currtime = GetTickCount();
  if (currtime - filter->last_frame_at < filter->milliseconds_between_frames) {
    return;
  }
  filter->last_frame_at = currtime;

  obs_source_t *parent = obs_filter_get_parent(filter->source);
  if (!parent) {
      return;
  }

  uint32_t base_width = obs_source_get_base_width(parent);
  uint32_t base_height = obs_source_get_base_height(parent);
  uint32_t width = 0;
  uint32_t height = 0;

  if (base_width != 0 && base_height != 0) {
    if ((float)base_width / (float)base_height < (float)filter->desired_width / (float)filter->desired_height) {
      width = filter->desired_width;
      height = (int)roundf((float)base_height * ((float)filter->desired_width / (float)base_width));
    } else {
      height = filter->desired_height;
      width = (int)roundf((float)base_width * ((float)filter->desired_height / (float)base_height));
    }
  }

  gs_texrender_reset(filter->texrender);

  if (gs_texrender_begin(filter->texrender, width, height)) {
    struct vec4 background;
    vec4_zero(&background);

    gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
    gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_BICUBIC);
    gs_eparam_t *dimension_param = gs_effect_get_param_by_name(effect, "base_dimension_i");
    struct vec2 dimension_i;
    vec2_set(&dimension_i, 1.0f / (float)width, 1.0f / (float)height);
    gs_eparam_t *undistort_factor_param = gs_effect_get_param_by_name(effect, "undistort_factor");
    float undistort_factor = 1.0f;
    if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
      gs_texrender_end(filter->texrender);
      return;
    }
    gs_effect_set_vec2(dimension_param, &dimension_i);
    gs_effect_set_float(undistort_factor_param, undistort_factor);
    obs_source_process_filter_tech_end(filter->source, effect, width, height, "Draw");

    gs_blend_state_pop();
    gs_texrender_end(filter->texrender);

    if (filter->width != width || filter->height != height) {
      gs_stagesurface_destroy(filter->stagesurface);
      filter->stagesurface = gs_stagesurface_create(width, height, GS_RGBA);
      filter->width = width;
      filter->height = height;
    }

    pthread_mutex_lock(&filter->write_mutex);
    if (filter->frame_data) {
      gs_stagesurface_unmap(filter->stagesurface);
      filter->frame_data = NULL;
    }
    gs_stage_texture(filter->stagesurface, gs_texrender_get_texture(filter->texrender));
    gs_stagesurface_map(filter->stagesurface, &filter->frame_data, &filter->frame_linesize);
    pthread_mutex_unlock(&filter->write_mutex);
    os_sem_post(filter->write_sem);
  }
}

static void *frame_capture_filter_create(obs_data_t *settings, obs_source_t *source)
{
  struct frame_capture_filter_data *filter = bzalloc(sizeof(*filter));
  wchar_t *appDataPath = bzalloc(sizeof(wchar_t) * MAX_PATH);
  wchar_t *gamePath = bzalloc(sizeof(wchar_t) * MAX_PATH);
  wchar_t *foundPath = 0;
  HRESULT hr = SHGetKnownFolderPath(&FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, NULL, &foundPath);
  if (hr != S_OK) {
    return NULL;
  }
  wcscpy_s(appDataPath, sizeof(wchar_t) * MAX_PATH, foundPath);
  wcscat_s(appDataPath, sizeof(wchar_t) * MAX_PATH, L"/Pursuit");
  _wmkdir(appDataPath);
  wcscat_s(appDataPath, sizeof(wchar_t) * MAX_PATH, L"/Captures");
  _wmkdir(appDataPath);
  wcscpy_s(gamePath, sizeof(wchar_t) * MAX_PATH, appDataPath);
  wcscat_s(gamePath, sizeof(wchar_t) * MAX_PATH, L"/");
  wcscat_s(gamePath, sizeof(wchar_t) * MAX_PATH, LONG_TEXT_OVERWATCH);
  _wmkdir(gamePath);
  wcscpy_s(gamePath, sizeof(wchar_t) * MAX_PATH, appDataPath);
  wcscat_s(gamePath, sizeof(wchar_t) * MAX_PATH, L"/");
  wcscat_s(gamePath, sizeof(wchar_t) * MAX_PATH, LONG_TEXT_FORTNITE);
  _wmkdir(gamePath);
  bfree(gamePath);
  CoTaskMemFree(foundPath);

  pthread_mutex_init_value(&filter->write_mutex);
  if (pthread_mutex_init(&filter->write_mutex, NULL) != 0) {
    pthread_mutex_destroy(&filter->write_mutex);
    bfree(filter);
    return NULL;
  }

  if (os_event_init(&filter->stop_event, OS_EVENT_TYPE_AUTO) != 0) {
    pthread_mutex_destroy(&filter->write_mutex);
    bfree(filter);
    return NULL;
  }

  if (os_sem_init(&filter->write_sem, 0) != 0) {
    pthread_mutex_destroy(&filter->write_mutex);
    os_event_destroy(filter->stop_event);
    bfree(filter);
    return NULL;
  }

  if (pthread_create(&filter->write_thread, NULL, write_thread, filter) != 0) {
    pthread_mutex_destroy(&filter->write_mutex);
    os_event_destroy(filter->stop_event);
    os_sem_destroy(filter->write_sem);
    bfree(filter);
    return NULL;
  }

  filter->source = source;
  filter->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
  filter->frame_data = NULL;
  filter->save_path = appDataPath;
  filter->frame_count = 0;
  DWORD currtime = GetTickCount();
  filter->last_frame_at = currtime;
  filter->current_folder = NULL;

  filter->previous_game = LONG_TEXT_OVERWATCH;
  filter->game = LONG_TEXT_OVERWATCH;
  filter->quality = OVERWATCH_QUALITY;
  filter->desired_width = OVERWATCH_DESIRED_WIDTH;
  filter->desired_height = OVERWATCH_DESIRED_HEIGHT;
  filter->frames_per_folder = OVERWATCH_FRAMES_PER_FOLDER;
  filter->milliseconds_between_frames = OVERWATCH_MILLISECONDS_BETWEEN_FRAMES;

  frame_capture_filter_update(filter, settings);
  obs_add_main_render_callback(frame_capture_filter_offscreen_render, filter);
  return filter;
}

static void frame_capture_filter_destroy(void *data)
{
  struct frame_capture_filter_data *filter = data;

  if (filter) {
    obs_remove_main_render_callback(frame_capture_filter_offscreen_render, filter);

    os_event_signal(filter->stop_event);
    os_sem_post(filter->write_sem);
    pthread_join(filter->write_thread, NULL);

    pthread_mutex_lock(&filter->write_mutex);
    gs_stagesurface_unmap(filter->stagesurface);
    gs_stagesurface_destroy(filter->stagesurface);
    gs_texrender_destroy(filter->texrender);
    pthread_mutex_unlock(&filter->write_mutex);

    pthread_mutex_destroy(&filter->write_mutex);
    os_event_destroy(filter->stop_event);
    os_sem_destroy(filter->write_sem);
    finish_folder(filter->current_folder, filter->previous_game, filter->save_path);
    bfree(filter->current_folder);
    bfree(filter->save_path);
    bfree(filter);
  }
}

static void frame_capture_filter_tick(void* data, float seconds)
{
    UNUSED_PARAMETER(seconds);
    UNUSED_PARAMETER(data);
}

static void frame_capture_filter_video_render(void* data, gs_effect_t* effect)
{
    UNUSED_PARAMETER(effect);
    struct frame_capture_filter_data *filter = data;
    obs_source_skip_video_filter(filter->source);
}

extern struct obs_source_info frame_capture_filter = {
  .id = "pursuit_frame_capture_filter",
  .type = OBS_SOURCE_TYPE_FILTER,
  .output_flags = OBS_SOURCE_VIDEO,
  .get_name = frame_capture_filter_name,
  .get_properties = frame_capture_filter_properties,
  .get_defaults = frame_capture_filter_defaults,
  .create = frame_capture_filter_create,
  .destroy = frame_capture_filter_destroy,
  .update = frame_capture_filter_update,
  .video_tick = frame_capture_filter_tick,
  .video_render = frame_capture_filter_video_render,
};

bool obs_module_load(void)
{
  obs_register_source(&frame_capture_filter);
  return true;
}