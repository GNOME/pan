/**
 * @defgroup data Backend Interfaces
 *
 * Interfaces for a newsreader's backend.
 *
 * This is the third lowest module, after "general" and "usenet-utils", and at
 * this level the code starts looking like something that might be used to build
 * a newsreader.
 *
 * This module defines objects and interfaces that can make up the backend of
 * a newsreader.  The primary interface class, `Data', defines an API for
 * accessing and setting a newsreader's backend data.
 *
 * Most of the other classes are either interfaces aggregated by data
 * (GroupServer, Profile) or are retrieved from Data (Article, ArticleTree).
 */

