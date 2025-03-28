-- used to construct the list of shown article of a group this is used
-- by my-tree.cc
create table if not exists article_view
  (
    id integer primary key asc autoincrement,
    article_id integer unique references article (id) on delete cascade,
    parent_id integer references article (id) on delete set null,
    init boolean default False,
    -- track status of article in article_view
    -- value is (n)ew: article was inserted
    -- (u)nchanged: article not changed
    -- (r)eparented: article reparented, i.e. its parent_id was changed
    --               following addition or deletion of its parent
    -- (d)elete: article was filtered out and will be deleted
    status text check (status in ('n','u','r','d')) not null
  );

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
