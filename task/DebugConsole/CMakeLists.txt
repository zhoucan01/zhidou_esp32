idf_component_register(SRCS "DebugConsole.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver DataCenter UsartExchange usrdug
                                FunctionSelection myLvgl heap)
