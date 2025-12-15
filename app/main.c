#include "../core/core.h"

int main(int argc, char **argv)
{
    ApplicationSpecification specification = create_specification();
    specification.argc = argc;
    specification.argv = argv;

    Application *app = create_application(&specification);

    init_application(app);

    int status = app->status;

    delete_application(app);
    return status;
}