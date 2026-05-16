#include "App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  App app(instance);
  return app.Run();
}
