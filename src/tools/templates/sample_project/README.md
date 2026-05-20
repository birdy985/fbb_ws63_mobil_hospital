# Sample project

A minimal, buildable HiSpark fbb project. It is the default template used by
`fbb create-project <name>`, which copies this tree to the target path under
the new project's name.

## Folder contents

```
├── CMakeLists.txt          Project entry; includes build_project.cmake
├── source
│   ├── CMakeLists.txt      Build config for the project's main component
│   └── app.c               Application entry point (app_main)
└── README.md               This file
```

## Usage

1. Put your application code in `source/app.c`.
2. Register any additional source files / headers in `source/CMakeLists.txt`.
3. Build from the project directory, with the fbb build environment active:

   ```
   fbb build <target>
   ```

`CMakeLists.txt` includes `build_project.cmake` from the SDK and calls
`cfbb_build_project()`; you normally do not need to edit it.
