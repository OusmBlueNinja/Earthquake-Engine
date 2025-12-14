#include "../core/core.h"

int main(int argc, char **argv)
{
    ApplicationSpecification specification;
    specification.window_size.x = 1280;
    specification.window_size.y = 720;

    specification.argc = argc;
    specification.argv = argv;

    Application *app = create_application(&specification);

    start_application(app);

    int status = app->status;

    delete_application(app);
    return status;
}