#ifndef ARG_SLIDER_H
#define ARG_SLIDER_H

class ArgSlider : public Gtk::HScale
{
public:
	ArgSlider(thArg *arg);
	~ArgSlider (void);

protected:
	virtual void on_value_changed ();

private:
	thArg *arg_;
};

#endif /* ARG_SLIDER_H */
