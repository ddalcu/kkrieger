// port: minimal sScript stub - the editor sScript is not linked into the player.
#pragma once
class sScript
{
public:
  void *Code;
  sScript() { Code = 0; }
  ~sScript() {}
  sBool Compile(const sChar *) { return sFALSE; }
  sBool Run() { return sFALSE; }
};
