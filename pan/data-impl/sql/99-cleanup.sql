-- cleanup table related to GUI state
delete from article_view;

-- check and remove orphaned article, i.e. articles that are no longer
-- attached any group and server, i.e. a group was removed or a server was removed
-- this should be taken care of at runtime, but let's cleanup on startup as well
delete from `article` where id in (
  select distinct a.id
    from `article` as a
         left outer join article_group as ag on ag.article_id == a.id
   where ag.article_id is null
);

