node test1 foo::foo {
   foo = 0;
   bar = test2->foo;
   point = test2->bar;
};
node test2 foo::bar {
   foo = 12;
   bar = test1->bar;
};
io test1;
name "test";
