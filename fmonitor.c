/* GKrellM (C) 1999-2000 Bill Wilson
|
|  Author:  Bill Wilson    bill@gkrellm.net
|  Latest versions might be found at:  http://gkrellm.net
|
|  This program is free software which I release under the GNU General Public
|  License. You may redistribute and/or modify this program under the terms
|  of that license as published by the Free Software Foundation; either
|  version 2 of the License, or (at your option) any later version.
|
|  This program is distributed in the hope that it will be useful,
|  but WITHOUT ANY WARRANTY; without even the implied warranty of
|  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
|  GNU General Public License for more details.
|
|  To get a copy of the GNU General Puplic License,  write to the
|  Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* File Monitor plugin for GKrellM
|
|  Copyright (C) 2000  Anatoly Asviyan
|
|  Author:  Anatoly Asviyan <aanatoly@linuxmail.org>
|
|  Version: for gkrellm version 0.10.4 and up
|
|  See README for details
*/


#include <sys/types.h>
#include <sys/wait.h>
#include <gkrellm2/gkrellm.h>
#include "fm_led.xpm"


static char *plugin_info[] =
{
"File Monitor plugin for GKrellM\n"
"Copyright (C) 2001 Anatoly Asviyan\n"
"aanatoly@linuxmail.org\n"
"Released under GPL\n"
"Modified by Jindrich Makovicka\n",
"makovick@kmlinux.fjfi.cvut.cz\n"
"\n"
"This plugin monitors a file and displays its contens in gkrellm. File can have\n"
"multiple rows of the form 'name : value : [flag]'. If flag non-empty then\n"
"for WARNING value - the orange led will light up and for ALERT value - red.\n"
"For example\n"
"  CPU:50.8:ALERT\n"
"  SBr:33.4:WARNING\n"
"  Fan1:4560:\n"
"or\n"
"  Temp:31 C:\n"
"  Hum:49 %:\n"
"\n"
"Plugin can monitor multiple files and for each file, you can specify\n"
"the following:\n",
"<i>Label", " - label of gkrellm panel\n",
"<i>File to monitor", " - file to monitor.\n",
"If preceded with a pipe (\"|\"), it will be instead executed in the shell\n",
"and the result will be read from standard output\n",
"<i>Update program", " - the program to update a monitored file.\n", 
"<i>Warning command", " - any shell command to run whenever warning flag is set.\n", 
"<i>Alert command", " - any shell command to run whenever alert flag is set.\n",
"<i>Check interval", " - how often to check.\n"
"\n",
"<b>CREDITS\n",
"Plugin is based on Bill Willson (bill@gkrellm.net) demo programs\n"
"and on fileread plugin by Henry Palonen (h@yty.net)\n"
"\n"
};


#define FM_CONFIG_NAME     "FMonitor"   /* Name in the configuration window */
#define FM_STYLE_NAME      "fmonitor"   /* Theme subdirectory name and gkrellmrc
 */

#define LEN             1024
#define MAXFILENUM        10
#define MAXPARAMNUM       10
#define MAXARGNUM         20

//#define HORIZONTALLY_CENTERED_LED

enum { FM_LABEL, FM_FILE, FM_UPDATE, FM_WARN, FM_ALERT, FM_INTERVAL, FM_ENTRY_NUM };

typedef struct {
    gchar *text[FM_ENTRY_NUM];
    gint pid; //pid of update command
    int ticker;
} fm_data;

static GkrellmMonitor *monitor;

typedef struct {
    GkrellmPanel *panel;
    GkrellmDecal *label;
    GkrellmDecal *led[MAXPARAMNUM];
    GkrellmDecal *name[MAXPARAMNUM];
    GkrellmDecal *value[MAXPARAMNUM];
    int fn[MAXPARAMNUM];
    int rownum, y;
} fm_gui;

static fm_data fmc[MAXFILENUM];
static fm_gui  fmg[MAXFILENUM];
static int fmnum = 0, cnum = 0, selrow = -1;

extern struct tm current_tm;

//static Panel    *panel;
//static Decal    *label_text;
static gint      style_id;
static gboolean	force_update;


static GdkPixmap *ledp, *ledm;

static GtkWidget *fm_vbox;
static GtkWidget *entry[FM_ENTRY_NUM];
static GtkWidget *config_list;
static GtkWidget *btn_enter, *btn_del;

static char *config_name[FM_ENTRY_NUM] = {
    "label", "file", "update", "warn", "alert", "interval"
};


#define MY_FREE(x) if(x) {g_free(x);(x)=NULL;}


static void destroy_fm_panels();
static void create_fm_panels(int first_create);
static void run_update_cmds();


static gint
panel_expose_event(GtkWidget *widget, GdkEventExpose *ev)
{
    int i;

    for (i = 0; i < fmnum; i++) {
		if (widget == fmg[i].panel->drawing_area)
			gdk_draw_pixmap(widget->window,
				widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
				fmg[i].panel->pixmap, ev->area.x, ev->area.y, ev->area.x, ev->area.y,
				ev->area.width, ev->area.height);
    }
    return FALSE;
}

/* Shamelessly ripped from the Gtk docs. */
static void fr_message(gchar *message)
{
    GtkWidget *dialog, *label, *okay_button;
    dialog = gtk_dialog_new();
    label = gtk_label_new (message);
    okay_button = gtk_button_new_with_label("OK");
    gtk_signal_connect_object (GTK_OBJECT (okay_button), "clicked",
		GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT(dialog));
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->action_area),
		okay_button);
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox),
		label);
    gtk_widget_show_all (dialog);
}

static void
destroy_decal(GkrellmPanel *p, GkrellmDecal *d)
{
	p->decal_list = g_list_remove(p->decal_list, d);
	gkrellm_destroy_decal(d);
}

static void
update_plugin()
{
    GkrellmStyle *style;
    GkrellmTextstyle *ts, *ts_alt;
    int x, h, r, i, rownum, ph, drawing_leds, changed = 0;
    int alert = 0, warning = 0;
    char s[LEN];
    FILE *fp;
	
    /* animation of alert and warning led */
    for (i = 0; i < fmnum; i++) {
	drawing_leds = FALSE;
	for (r = 0; r < fmg[i].rownum; ++r) {
	    if (fmg[i].fn[r] < 2) {
		x = ((GK.timer_ticks % 10) < 2) ? 2 : fmg[i].fn[r];
		gkrellm_draw_decal_pixmap(fmg[i].panel, fmg[i].led[r],x);
		drawing_leds = TRUE;
	    }
	}
	if (drawing_leds)
	    gkrellm_draw_panel_layers(fmg[i].panel);
    }		

    if (!GK.second_tick)
	return;

    /* time to check monitored file(s) */
    force_update = FALSE;
    style = gkrellm_meter_style(style_id);
    ts = gkrellm_meter_textstyle(style_id);
    ts_alt = gkrellm_meter_alt_textstyle(style_id);

    for (i = 0; i < fmnum; i++) {
	int interval = atoi(fmc[i].text[FM_INTERVAL]);
	if (interval < 1 || interval > 3600) interval = 60;

	if (++fmc[i].ticker >= interval) {
	    fmc[i].ticker = 0;
	} else {
	    continue;
	}

	ph = fmg[i].panel->h;
	rownum = 0;
	if (fmc[i].text[FM_FILE][0] == '|') {
//		    fprintf(stderr, "popen %s\n", fmc[i].text[FM_FILE]+1);
	    fp = popen(fmc[i].text[FM_FILE]+1, "r");
	} else {
	    fp = fopen(fmc[i].text[FM_FILE], "r");
	}
	if (fp) {
	    /* read at most MAXPARAMNUM valid rows from file */
	    while (fgets(s, LEN, fp) && (rownum < MAXPARAMNUM)) {
		char *sname, *svalue, *sled;

//				fprintf(stderr, "str %s\n", s);

		sname = strtok(s, ":");
		if (!sname || *sname == '\0')
		    continue;
		svalue = strtok(NULL, ":");
		if (!svalue || *svalue == '\0')
		    continue;
		sled = strtok(NULL, " \n\t");
		rownum++;
		/* create decals for new row */
		if (rownum > fmg[i].rownum) {
		    changed = 1;
		    r = rownum - 1;
#ifdef HORIZONTALLY_CENTERED_LED
		    /* Create led first so it will be behind name and value */
		    /* name at left margin, value at right margin, led center */
		    fmg[i].led[r] = gkrellm_create_decal_pixmap(fmg[i].panel,
								ledp, ledm, 3, style, 0, fmg[i].y);		
		    fmg[i].led[r]->x = (gkrellm_chart_width() - fmg[i].led[r]->w) / 2;
		    fmg[i].name[r] = gkrellm_create_decal_text(fmg[i].panel,
							       "VCOR2", ts_alt, style, -1, fmg[i].y, 0);
		    fmg[i].value[r] = gkrellm_create_decal_text(fmg[i].panel,
								"5555", ts_alt, style, 0, fmg[i].y, 0);
		    fmg[i].value[r]->x = gkrellm_chart_width() - fmg[i].value[r]->w
			- style->margin;
#else
		    fmg[i].led[r] = gkrellm_create_decal_pixmap(fmg[i].panel,
								ledp, ledm, 3, style, -1, fmg[i].y);		
		    x = style->margin.left + 5;
		    fmg[i].name[r] = gkrellm_create_decal_text(fmg[i].panel,
							       "VCOR2", ts_alt, style, x, fmg[i].y, 0);
		    //					fmg[i].value[r] = gkrellm_create_decal_text(fmg[i].panel,
		    //						"5555", ts_alt, style, x+fmg[i].name[r]->w, fmg[i].y, 0);
		    /* Bill: Do you think value decal should be at right margin ? */
		    /* Anatoly: Yes I do, I added it to TODO list */
		    fmg[i].value[r] = gkrellm_create_decal_text(fmg[i].panel,
								"5555", ts_alt, style, 0, fmg[i].y, 0);
		    fmg[i].value[r]->x = gkrellm_chart_width()
			- fmg[i].value[r]->w - style->margin.left;
#endif
		    h = fmg[i].name[r]->h;
		    if (fmg[i].value[r]->h > h)
			h = fmg[i].value[r]->h;
		    if (fmg[i].led[r]->h < h)	/* vertically center led */
			fmg[i].led[r]->y += (h - fmg[i].led[r]->h) / 2;
		    fmg[i].y += h + 1;

		    fmg[i].rownum++;

		}
				
		if (!sled || !(*sled)) {
		    fmg[i].fn[rownum-1] = 2;
		} else if (!strcmp(sled, "WARNING")) {
		    fmg[i].fn[rownum-1] = 1;
		    warning = 1;
		} else {
		    fmg[i].fn[rownum-1] = 0;
		    alert = 1;
		}
		gkrellm_draw_decal_pixmap(fmg[i].panel, fmg[i].led[rownum-1], fmg[i].fn[rownum-1]);
//				fprintf(stderr, "update led pixmap\n");
		gkrellm_draw_decal_text(fmg[i].panel, fmg[i].name[rownum-1], sname, -1);
		gkrellm_draw_decal_text(fmg[i].panel, fmg[i].value[rownum-1], svalue, -1);

	    }
	    if (fmc[i].text[FM_FILE][0] == '|') {
		pclose(fp);
	    } else {
		fclose(fp);
	    }
	}
	/* destroys unused decals */
	while (fmg[i].rownum > rownum) {
	    changed = 1;
	    h = fmg[i].name[fmg[i].rownum-1]->h;
	    if (fmg[i].value[fmg[i].rownum-1]->h > h)
		h = fmg[i].value[fmg[i].rownum-1]->h;
	    fmg[i].y -= h + 1;
	    destroy_decal(fmg[i].panel, fmg[i].led[fmg[i].rownum-1]);
	    destroy_decal(fmg[i].panel, fmg[i].name[fmg[i].rownum-1]);
	    destroy_decal(fmg[i].panel, fmg[i].value[fmg[i].rownum-1]);
	    fmg[i].rownum--;
	}

	//gkrellm_monitor_height_adjust(fmg[i].panel->h - ph);
	if (changed) {
	    gkrellm_panel_configure(fmg[i].panel, NULL, style);
	    gkrellm_panel_create(fm_vbox, monitor, fmg[i].panel);
	    if (fmc[i].text[FM_LABEL]) {
		gkrellm_draw_decal_text(fmg[i].panel, fmg[i].label,
					fmc[i].text[FM_LABEL], -1);
	    }
	}
	if (warning)
	    system(fmc[i].text[FM_WARN]);
	if (alert)
	    system(fmc[i].text[FM_ALERT]);
	gkrellm_draw_panel_layers(fmg[i].panel);
    }

    return;
}

/* creates "Sensors" monitor panel. the panel consists of label,
 * and led/name/value pair for each row found in rc_file
 */
static void
create_plugin(GtkWidget *vbox, gint first_create)
{
    fm_vbox = vbox;
    create_fm_panels(first_create);
	force_update = TRUE;
}

static void
destroy_fm_panels()
{
    int i;

    for (i = 0; i < fmnum; i++) {
		if (fmg[i].panel) {
//			gkrellm_monitor_height_adjust(-fmg[i].panel->h);
//			gkrellm_destroy_decal_list(fmg[i].panel);
//			gkrellm_destroy_krell_list(fmg[i].panel);
			gkrellm_panel_destroy(fmg[i].panel);
//			g_free(fmg[i].panel);
			fmg[i].panel = NULL;
			fmg[i].rownum = 0;
		}
    }
}

static void
create_fm_panels(int first_create)
{
    GkrellmStyle *style;
    GkrellmTextstyle *ts, *ts_alt;
    int i;
    GkrellmPiximage *led = NULL;

    style = gkrellm_meter_style(style_id);
    ts = gkrellm_meter_textstyle(style_id);
    ts_alt = gkrellm_meter_alt_textstyle(style_id);

	/* gkrellm_load_image will kill red_led and orange_led if not NULL */
    gkrellm_load_piximage("fm_led", fm_led_xpm, &led, FM_STYLE_NAME);
    gkrellm_scale_piximage_to_pixmap(led, &ledp, &ledm, 0, 0);

    for (i = 0; i < fmnum; i++) {
		//int y;

		if (first_create)
			fmg[i].panel = gkrellm_panel_new0();
		else {
			gkrellm_destroy_krell_list(fmg[i].panel);
			gkrellm_destroy_decal_list(fmg[i].panel);
			fmg[i].rownum = 0;
		}
		fmg[i].panel->textstyle = ts;        /* would be used for a panel label */

		fmg[i].y = 0;
		if (fmc[i].text[FM_LABEL]) {
			fmg[i].label = gkrellm_create_decal_text(fmg[i].panel,
				fmc[i].text[FM_LABEL], ts, style, -1, -1, -1);
			fmg[i].y = fmg[i].label->y + fmg[i].label->h;
		}
		else  /* If no label, start remaining decals at top margin */
			gkrellm_get_top_bottom_margins(style, &fmg[i].y, NULL);

		gkrellm_panel_configure(fmg[i].panel, NULL, style);
		//panel->label->h_panel += 2;         /* Some bottom margin */
		gkrellm_panel_create(fm_vbox, monitor, fmg[i].panel);

		if (fmc[i].text[FM_LABEL]) {
			gkrellm_draw_decal_text(fmg[i].panel, fmg[i].label,
				fmc[i].text[FM_LABEL], 1);
		}
		if (first_create)
			gtk_signal_connect(GTK_OBJECT (fmg[i].panel->drawing_area), "expose_event",
				(GtkSignalFunc) panel_expose_event, NULL);
		gkrellm_draw_panel_layers(fmg[i].panel);
    }
	if (first_create)
		run_update_cmds();
}




static void
item_sel(GtkWidget *clist, gint row, gint column,
    GdkEventButton *event,
    gpointer data )
{
    gchar *tmp;
    int i;

    selrow = row;
    for (i = 0; i < FM_ENTRY_NUM; i++) {
		if (gtk_clist_get_text(GTK_CLIST(config_list), row, i, &tmp)) {
			gtk_entry_set_text(GTK_ENTRY(entry[i]), tmp);
		} else {
			fprintf(stderr, "Strange: can't read %d-th col data "
				"of %d selected row\n", i, row);
		}
    }
    gtk_widget_set_sensitive(btn_del, TRUE);
    return;
}


static void
item_unsel(GtkWidget *clist, gint row, gint column, GdkEventButton *event,
    gpointer data )
{
    int i;

    selrow = -1;
    for (i = 0; i < FM_ENTRY_NUM; i++) {
		gtk_entry_set_text(GTK_ENTRY(entry[i]), "");
    }

    gtk_widget_set_sensitive(btn_del, FALSE);
    return;
}

static void
on_add_click(GtkButton *button, gpointer *data)
{
    char str[80];
    int  i;
    fm_data c;

    if (!*gtk_entry_get_text(GTK_ENTRY(entry[FM_FILE]))) {
		sprintf(str, "You must specify file to monitor.\n");
		fr_message(str);
		return;
    }


    if (selrow >= 0) {
		/* we're editing existing entry */
		gtk_clist_freeze( GTK_CLIST(config_list) );
		for (i = 0; i < FM_ENTRY_NUM; i++) {
			gtk_clist_set_text(GTK_CLIST(config_list), selrow, i,
				gtk_entry_get_text(GTK_ENTRY(entry[i])));
		}
		gtk_clist_thaw( GTK_CLIST(config_list) );
    } else {
		/* we're adding new row */
		if (cnum == MAXFILENUM) {
			sprintf(str, "Maximum (%d) files has reached.\nSorry.\n",
				MAXFILENUM);
			fr_message(str);
			return;
		}
		cnum++;

		for (i = 0; i < FM_ENTRY_NUM; i++) {
			c.text[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry[i])));
		}
		gtk_clist_append (GTK_CLIST(config_list), c.text);
		for (i = 0; i < FM_ENTRY_NUM; i++) {
		    g_free(c.text[i]);
		}
    }

    return;
}


static void
on_del_click(GtkButton *button, gpointer *data)
{
    if (selrow == -1) return;

    gtk_clist_remove(GTK_CLIST(config_list), selrow);
    cnum--;
}


static void
create_config_tab(GtkWidget *tab_vbox)
{
    GtkWidget		*vbox;
    GtkWidget		*tabs;
    GtkWidget               *text;
    GtkWidget               *btn_hbox;
    gchar                   *titles[]={"Lable","File",
				       "Update Command",
				       "Warning Command",
				       "Alert Command",
				       "Interval"};
    GtkWidget *edit_table, *scrolled_window;
    GtkWidget *llabel, *lfile, *lupdate, *lwarn, *lalert, *linterval, *linterval2;

    gint                    i;
    //item_t                  *item;

    tabs = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

    /* Preferences Tab */
    vbox = gkrellm_gtk_notebook_page(tabs, "Preferences");

    // Edit table
    edit_table = gtk_table_new(3, 5, FALSE);
    llabel = gtk_label_new("Label:");
    gtk_misc_set_alignment (GTK_MISC (llabel), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), llabel, 0, 1, 0, 1, GTK_FILL, 0, 1, 
1);

    entry[FM_LABEL] = gtk_entry_new_with_max_length(9);
    gtk_table_attach(GTK_TABLE(edit_table), entry[FM_LABEL], 1, 2, 0, 1, 0, 0, 1
, 1);
    llabel = gtk_label_new(" ");
    gtk_misc_set_alignment (GTK_MISC (llabel), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), llabel, 2, 3, 0, 1,
		GTK_FILL|GTK_EXPAND, 0, 1, 1);


    lfile = gtk_label_new("File to monitor:");
    gtk_misc_set_alignment (GTK_MISC (lfile), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), lfile,  0, 1, 1, 2, GTK_FILL, 0, 1, 
1);

    entry[FM_FILE] = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(edit_table), entry[FM_FILE], 1, 3, 1, 2,
		GTK_FILL|GTK_EXPAND, 0, 1, 1);

    lupdate = gtk_label_new("Update Command:");
    gtk_misc_set_alignment (GTK_MISC (lupdate), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), lupdate, 0, 1, 2, 3, GTK_FILL, 0, 1,
 1);

    entry[FM_UPDATE] = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(edit_table), entry[FM_UPDATE], 1, 3, 2, 3,
		GTK_FILL, 0, 1, 1);

    lwarn = gtk_label_new("Warning Command:");
    gtk_misc_set_alignment (GTK_MISC (lwarn), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), lwarn, 0, 1, 3, 4, GTK_FILL, 0, 1, 1
);

    entry[FM_WARN] = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(edit_table), entry[FM_WARN], 1, 3, 3, 4,
		GTK_FILL, 0, 1, 1);

    lalert = gtk_label_new("Alert Command:");
    gtk_misc_set_alignment (GTK_MISC (lalert), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), lalert, 0, 1, 4, 5, GTK_FILL, 0, 1, 1);

    entry[FM_ALERT] = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(edit_table), entry[FM_ALERT], 1, 3, 4, 5,
		GTK_FILL, 0, 1, 1);

    linterval = gtk_label_new("Check Interval:");
    gtk_misc_set_alignment (GTK_MISC (linterval), 1, 1);
    gtk_table_attach(GTK_TABLE(edit_table), linterval, 0, 1, 5, 6, GTK_FILL, 0, 1, 1);

    entry[FM_INTERVAL] = gtk_entry_new_with_max_length(4);
    gtk_table_attach(GTK_TABLE(edit_table), entry[FM_INTERVAL], 1, 2, 5, 6,
		GTK_FILL, 0, 1, 1);

    linterval2 = gtk_label_new("seconds");
    gtk_misc_set_alignment (GTK_MISC (linterval2), 0, 1);
    gtk_table_attach(GTK_TABLE(edit_table), linterval2, 2, 3, 5, 6, GTK_FILL, 0, 1, 1);

    // Buttons to accept/reject config. data in the edit box
    btn_hbox = gtk_hbox_new(FALSE, 5);
    btn_enter=gtk_button_new_with_label("Enter");
    gtk_signal_connect(GTK_OBJECT(btn_enter), "clicked",
		GTK_SIGNAL_FUNC(on_add_click),NULL);
    btn_del=gtk_button_new_with_label("Delete");
    gtk_widget_set_sensitive(btn_del, FALSE);
    gtk_signal_connect(GTK_OBJECT(btn_del), "clicked",
		GTK_SIGNAL_FUNC(on_del_click),NULL);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_enter, TRUE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_del, TRUE, FALSE, 2);

    // List with saved configurations
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
		GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);

    config_list = gtk_clist_new_with_titles(6,titles);
    gtk_container_add(GTK_CONTAINER(scrolled_window), config_list);
    gtk_signal_connect(GTK_OBJECT(config_list), "select-row",
		GTK_SIGNAL_FUNC(item_sel), NULL);
    gtk_signal_connect(GTK_OBJECT(config_list), "unselect-row",
		GTK_SIGNAL_FUNC(item_unsel), NULL);
    gtk_clist_set_selection_mode(GTK_CLIST(config_list), GTK_SELECTION_SINGLE);
    for (i=0; i<6; i++) {
		int width;
		switch (i) {
		case 0: width=70;break;
		case 1: width=80;break;
		default: width=150;break;
		}
		gtk_clist_set_column_width(GTK_CLIST(config_list), i, width);
    }
    for (i = 0; i < fmnum; i++)
		gtk_clist_append(GTK_CLIST(config_list), fmc[i].text);
    cnum = fmnum;
    //gtk_clist_columns_autosize(GTK_CLIST(config_list));


    gtk_box_pack_start(GTK_BOX(vbox), edit_table, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), btn_hbox, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 2);

    /* Info Tab */
    vbox = gkrellm_gtk_notebook_page(tabs, "Info");
    text = gkrellm_gtk_scrolled_text_view(vbox, NULL, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gkrellm_gtk_text_view_append_strings(text,plugin_info,sizeof(plugin_info)/sizeof(gchar *));

}


static void
save_config(FILE *f)
{
    int i, j;
    for (i = 0; i < fmnum; i++) {
		for (j = 0; j < FM_ENTRY_NUM; j++) {
			fprintf(f, "FMonitor %s:%d:%s\n", config_name[j], i,
				fmc[i].text[j] ? fmc[i].text[j] : "");
		}
    }

}

static void
load_config( gchar* arg )
{
    char *tmp, *tmp2,  *name, *value;
    int rowno, i;

    tmp = g_strdup(arg);

    name = strtok(tmp, ":");
    if (!name) return;

    tmp2 = strtok(NULL, ":");
    if (!tmp2) return;
    rowno = atoi(tmp2);
    if (rowno < MAXFILENUM) {
		value = strtok(NULL, "\n");

		for (i = 0; i < FM_ENTRY_NUM; i++) {
			if (!strcmp(name, config_name[i])) {
				if (value)
					fmc[rowno].text[i] =  g_strdup(value);
				else
					fmc[rowno].text[i] =  g_strdup("");
			}
		}

		fmc[rowno].ticker = 10000;

		if (rowno+1 > fmnum) fmnum = rowno+1;
    }
    g_free(tmp);
}

static void
del_fmc_entries()
{
    int i, no;

	for (no = 0; no < fmnum; no++) {
		for (i = 0; i < FM_ENTRY_NUM; i++) {
			MY_FREE(fmc[no].text[i]);
		}
	}
}
  

static void
run_update_cmds()
{
	int i, pid, ac;
	char *arg[MAXARGNUM], *cmd;

	//fprintf(stderr, "run_update_cmds\n");
	
	for (i = 0; i < fmnum; i++) {
		cmd = g_strdup(fmc[i].text[FM_UPDATE]);
		ac = 0;
		arg[ac] = strtok(cmd, " \n\t");
		while ((ac < MAXARGNUM-1) && arg[ac]) 
			arg[++ac] = strtok(NULL, " \n\t");
		if (arg[0] && *(arg[0])) {
			pid = fork();
			if (pid) {
				//papa
				fmc[i].pid = pid;
				g_free(cmd);
			} else {
				//son
				execvp(arg[0], arg);
				fprintf(stderr, "Can't execvp <%s>\n", fmc[i].text[FM_UPDATE]);
				_exit(1);
			}
		}
	}
}
	 
static void
kill_update_cmds()
{
	int i;

	//fprintf(stderr, "kill_update_cmds\n");
	
	for (i = 0; i < fmnum; i++) {
		if (fmc[i].pid) {
			//fprintf(stderr, "\tkilling %d\n", fmc[i].pid);
			kill(fmc[i].pid, SIGKILL);
		}
	}
}


static void
apply_config()
{
    char *tmp;
    int i;

    selrow = -1;

	//fprintf(stderr, "apply_config\n");
	
    item_unsel(GTK_WIDGET(config_list), 0, 0, NULL, NULL);
	del_fmc_entries();
	kill_update_cmds();
    destroy_fm_panels();

    fmnum = 0;
    while (gtk_clist_get_text(GTK_CLIST(config_list), fmnum, 0, &tmp))    {
		for (i = 0; i < FM_ENTRY_NUM; i++) {
			if (gtk_clist_get_text(GTK_CLIST(config_list), fmnum, i, &tmp))
				fmc[fmnum].text[i] = g_strdup(tmp);
		}
		fmc[fmnum].ticker = 10000;
		fmnum++;
		if (fmnum == MAXFILENUM)
			break;
    }

    create_fm_panels(1);
	force_update = TRUE;
}


static void my_wait(int signo)
{
	waitpid(-1, NULL, WNOHANG | WUNTRACED);
}

		 

/* The monitor structure tells GKrellM how to call the plugin routines.
*/
static GkrellmMonitor  plugin_mon =
{
    FM_CONFIG_NAME,     /* Name, for config tab.    */
    0,                  /* Id,  0 if a plugin       */
    create_plugin,      /* The create function      */
    update_plugin,      /* The update function      */
    create_config_tab,  /* The config tab create function   */
    apply_config,       /* Apply the config function        */

    save_config,        /* Save user config     */
    load_config,        /* Load user config     */
    FM_CONFIG_NAME,     /* config keyword       */

    NULL,               /* Undefined 2  */
    NULL,               /* Undefined 1  */
    NULL,               /* private      */

    MON_MAIL,           /* Insert plugin before this monitor            */

    NULL,               /* Handle if a plugin, filled in by GKrellM     */
    NULL                /* path if a plugin, filled in by GKrellM       */
};


/* All GKrellM plugins must have one global routine named init_plugin()
  |  which returns a pointer to a filled in monitor structure.
  */
GkrellmMonitor *
gkrellm_init_plugin()
{

    style_id = gkrellm_add_meter_style(&plugin_mon, FM_STYLE_NAME);
    atexit(kill_update_cmds);
    signal(SIGCHLD, my_wait);
    monitor = &plugin_mon;
    return &plugin_mon;
}

