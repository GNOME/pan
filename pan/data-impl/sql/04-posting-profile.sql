-- given that user will have few profile, don't bother with indexes
create table if not exists profile (
  id integer primary key asc autoincrement,
  name text non null unique,

  server_id integer references server(id) on delete cascade,

  author_id integer not null references author (id) on delete restrict,

  -- avatars
  face text,
  xface text,

  -- optional info, extra headers are stored in profile_header table
  attribution,
  -- message-id domain name
  fqdn
);

create table if not exists profile_header (
  id integer primary key asc autoincrement,
  profile_id integer references profile(id) on delete cascade,
  name text non null,
  value text non null
);

create table if not exists signature (
  id integer primary key asc autoincrement,
  profile_id integer unique references profile(id) on delete cascade,
  active boolean check(active in (False,True)),

  -- gpgsig type only used with GMIME_CRYPTO
  type text check (type in ("text","command","file","gpgsig")),
  content text,
  gpg_sig_uid text
);
