name "test";

node test1 test::test {
   foo = 0;
   bar = test2->foo;
   point = test2->bar;
};
node test2 test::test {
   foo = 12;
   bar = test1->bar;
};
io test1;
