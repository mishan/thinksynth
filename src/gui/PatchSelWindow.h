#ifndef PATCHSEL_WINDOW_H
#define PATCHSEL_WINDOW_H

class PatchSelWindow : public Gtk::Window
{
public:
	PatchSelWindow (void);

protected:
	Gtk::VBox vbox;

	Gtk::Table patchTable;	
private:
};

#endif /* PATCHSEL_WINDOW_H */
