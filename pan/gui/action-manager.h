#ifndef ACTION_MANAGER_H
#define ACTION_MANAGER_H

/**
 * Interface for objects that encapsulate GTK action groups.
 * @ingroup GUI
 */
struct ActionManager
{
  ActionManager () {}
  virtual ~ActionManager () {}

  virtual bool is_action_active (const char * action) const = 0;
  virtual void activate_action (const char * action) const = 0;
  virtual void toggle_action (const char * action, bool) const = 0;
  virtual void sensitize_action (const char * action, bool) const = 0;
  virtual void hide_action (const char * key, bool b) const = 0;
  virtual GtkWidget* get_action_widget (const char * key) const = 0;
  virtual void disable_accelerators_when_focused (GtkWidget * entry) const = 0;
};

#endif
