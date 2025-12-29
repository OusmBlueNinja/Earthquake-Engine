#include "../core/core.h"
#include "demo_layer.h"
#include "editor_layer.h"

int main(int argc, char **argv)
{
    ApplicationSpecification specification = create_specification();
    specification.argc = argc;
    specification.argv = argv;

    specification.vsync = WM_VSYNC_OFF;

    Application *app = create_application(&specification);


    push_layer(create_editor_layer());
    push_layer(create_demo_layer());

    init_application(app);

    const int status = app->status;

    delete_application(app);
    return status;
}