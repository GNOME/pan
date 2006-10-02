
stock_images = \
	icon_article_new.png \
	icon_article_read.png \
	icon_article_unread.png \
	icon_binary_complete.png \
	icon_binary_complete_read.png \
	icon_binary_incomplete.png \
	icon_binary_incomplete_read.png \
	icon_bluecheck.png \
	icon_blue_flag.png \
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
	icon_filter_only_new.png \
	icon_filter_only_watched.png \
	icon_get_dialog.png \
	icon_get_flagged.png \
	icon_get_new.png \
	icon_get_selected.png \
	icon_get_subscribed.png \
	icon_layout_1.png \
	icon_layout_2.png \
	icon_layout_3.png \
	icon_layout_4.png \
	icon_layout_5.png \
	icon_online.png \
	icon_pan_logo.png \
	icon_pan_about_logo.png \
	icon_pan.png \
	icon_read_group.png \
	icon_read_more.png \
	icon_read_less.png \
	icon_read_new_article.png \
	icon_read_new_thread.png \
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
	icon_zoom_in.png \
	icon_zoom_out.png
    

EXTRA_DIST = \
	pan-template.xcf \
	$(stock_images)

noinst_HEADERS = \
	pan-pixbufs-internal.h \
	pan-pixbufs.h

pan-pixbufs-internal.h: $(stock_images)
		-rm -f pan-pixbufs-internal.h
		var=`echo "$(stock_images)" | sed -e 's,icon_\([^\.]*\)\.png,icon_\1 $(srcdir)/icon_\1.png,g'` ; \
		   echo $$var; \
		   gdk-pixbuf-csource --extern --raw --build-list $$var > pan-pixbufs-internal.h

pan-pixbufs.h: pan-pixbufs-internal.h
		-rm -f pan-pixbufs.h
		cat pan-pixbufs-internal.h | grep "const guint8 icon_" | grep -v pragma | grep -v __attribute | sed "s,^\([a-z0-9 _]*\[\]\) =,extern \1;," > pan-pixbufs.h
