int main()
{
try
{
throw("boom");
}
catch
{
if(getExceptionInfo()!="boom")return 1;
}
return example_add(2,3)+int(example_echo("ab").length());
}
