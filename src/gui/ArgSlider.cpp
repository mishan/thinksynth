#include "config.h"

#include <gtkmm.h>

#include "think.h"

#include "ArgSlider.h"

ArgSlider::ArgSlider (thArg *arg)
	: Gtk::HScale (arg->argMin, arg->argMax, .01)
{
	arg_ = arg;
	set_value (arg_->argValues[0]);
	set_draw_value();
	set_value_pos(Gtk::POS_LEFT);
}

ArgSlider::~ArgSlider ()
{
}

void ArgSlider::on_value_changed ()
{
	arg_->argValues[0] = get_value();
}
