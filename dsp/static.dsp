name "static";

node ionode test::test {
	out = static->out;
};
node static osc::static {
};

io ionode;
