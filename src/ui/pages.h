#ifndef PITCHRATE_UI_PAGES_H
#define PITCHRATE_UI_PAGES_H

#include "app.h"

Page *dashboard_page_create(App *app);
Page *players_page_create(App *app);
Page *teams_page_create(App *app);
Page *matches_page_create(App *app);
Page *guide_page_create(App *app);

#endif
