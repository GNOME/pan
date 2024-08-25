create table if not exists server (
    id integer primary key asc autoincrement,
    host text not null,
    port integer check(port between 1 and 65535),
    -- match the id found in servers.xml
    pan_id text unique not null,
    username text,
    password text,
    expiry_days integer integer check(expiry_days >= 0),
    connection_limit integer check(connection_limit >= 0),
    newsrc_filename text not null,
    rank integer,
    use_ssl boolean check(use_ssl in (False, True)),
    trust_certificate boolean check(trust_certificate in (False, True)),
    compression_type integer,
    certificate text
);

create unique index if not exists server_pan_id on server (pan_id);
create unique index if not exists server_host on server (host);

-- create a pseudo server to host local folders managed by Pan like
-- Drafts and Sent
insert into server (host, port, pan_id, newsrc_filename)
            values('local',1,0,'/dev/null') on conflict do nothing;

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
