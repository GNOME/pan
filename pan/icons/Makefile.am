NULL =

public_icons_themes = \
	hicolor \
	$(NULL)

public_icons = \
	hicolor_apps_16x16_org.gnome.pan.png \
	hicolor_apps_24x24_org.gnome.pan.png \
	hicolor_apps_32x32_org.gnome.pan.png \
	hicolor_apps_48x48_org.gnome.pan.png \
	hicolor_apps_256x256_org.gnome.pan.png \
	$(NULL)

stock_images = \
	icon_article_read.png \
	icon_article_unread.png \
	icon_binary_complete.png \
	icon_binary_complete_read.png \
	icon_binary_incomplete.png \
	icon_binary_incomplete_read.png \
	icon_bluecheck.png \
	icon_red_flag.png \
	icon_by_me.png \
	icon_by_others.png \
	icon_compose_followup.png \
	icon_compose_post.png \
	icon_disk.png \
	icon_empty.png \
	icon_filter.png \
	icon_filter_only_attachments.png \
	icon_filter_only_cached.png \
	icon_filter_only_me.png \
	icon_filter_only_read.png \
	icon_filter_only_unread.png \
	icon_filter_only_watched.png \
	icon_get_dialog.png \
	icon_get_flagged.png \
	icon_get_selected.png \
	icon_get_subscribed.png \
	icon_layout_1.png \
	icon_layout_2.png \
	icon_layout_3.png \
	icon_layout_4.png \
	icon_layout_5.png \
	icon_pan_about_logo.png \
	icon_pan.png \
	icon_read_group.png \
	icon_read_more.png \
	icon_read_less.png \
	icon_read_unread_article.png \
	icon_read_unread_thread.png \
	icon_score.png \
	icon_search_pulldown.png \
	icon_x.png \
	icon_mozilla_frown.png \
	icon_mozilla_smile.png \
	icon_mozilla_surprised.png \
	icon_mozilla_tongueout.png \
	icon_mozilla_wink.png \
	icon_plain.png \
	icon_cert.png \
	icon_status_online.png \
  icon_status_offline.png \
  icon_status_active.png \
  icon_status_queue_empty.png \
  icon_status_error.png \
  icon_status_idle.png \
  icon_status_new_articles.png \
  icon_sig_ok.png \
  icon_sig_fail.png \
  icon_show_signature.png \
  icon_prefs_applications.png \
  icon_prefs_extras.png \
  icon_prefs_hotkeys.png \
  icon_prefs_behavior.png \
  icon_prefs_fonts.png \
  icon_prefs_upload.png \
  icon_prefs_colors.png \
  icon_prefs_headers.png \
  icon_prefs_layout.png \
  icon_prefs_actions.png \
  icon_prefs_panes.png \
  icon_expand_thread.png \
  icon_collapse_thread.png \
  $(NULL)

noinst_DATA = \
	Pan.ico \
	$(NULL)

EXTRA_DIST = \
	$(public_icons) \
	$(stock_images) \
	$(noinst_DATA) \
	$(NULL)

noinst_HEADERS = \
	pan-pixbufs-internal.h \
	pan-pixbufs.h \
	$(NULL)

gtk_update_icon_cache = gtk-update-icon-cache -f -t

update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		for theme in $(public_icons_themes); do \
			$(gtk_update_icon_cache) $(datadir)/icons/$$theme; \
		done; \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		for theme in $(public_icons_themes); do \
			echo "***   $(gtk_update_icon_cache) $(datadir)/icons/$$theme"; \
		done; \
	fi

install-icons:
	for icon in $(public_icons); do \
		THEME=`echo $$icon | cut -d_ -f1`; \
		CONTEXT=`echo $$icon | cut -d_ -f2`; \
		SIZE=`echo $$icon | cut -d_ -f3`; \
		ICONFILE=`echo $$icon | cut -d_ -f4`; \
		mkdir -p $(DESTDIR)$(datadir)/icons/$$THEME/$$SIZE/$$CONTEXT; \
		$(INSTALL_DATA) $(srcdir)/$$icon $(DESTDIR)$(datadir)/icons/$$THEME/$$SIZE/$$CONTEXT/$$ICONFILE; \
	done

uninstall-icons:
	-for icon in $(public_icons); do \
		THEME=`echo $$icon | cut -d_ -f1`; \
		CONTEXT=`echo $$icon | cut -d_ -f2`; \
		SIZE=`echo $$icon | cut -d_ -f3`; \
		ICONFILE=`echo $$icon | cut -d_ -f4`; \
		rm -f $(DESTDIR)$(datadir)/icons/$$THEME/$$SIZE/$$CONTEXT/$$ICONFILE; \
	done

install-data-local: install-icons
install-data-hook: update-icon-cache

uninstall-local: uninstall-icons
uninstall-hook: update-icon-cache

pan-pixbufs-internal.h: $(stock_images)
		-rm -f pan-pixbufs-internal.h
		var=`echo "$(stock_images)" | sed -e 's,icon_\([^\.]*\)\.png,icon_\1 $(srcdir)/icon_\1.png,g'` ; \
		echo $$var; \
		gdk-pixbuf-csource --extern --raw --build-list $$var > pan-pixbufs-internal.h

pan-pixbufs.h: pan-pixbufs-internal.h
		-rm -f pan-pixbufs.h
		cat pan-pixbufs-internal.h | grep "const guint8 icon_" | grep -v pragma | grep -v __attribute | sed "s,^\([a-z0-9 _]*\[\]\) =,extern \1;," > pan-pixbufs.h
