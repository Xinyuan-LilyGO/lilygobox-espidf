if(CONFIG_DEVICE_T_GLASSES_P4)
    set(
        project_path
        "./scr/"
        "./device/t-glasses-p4/"
    )
endif()
        
idf_component_register(SRC_DIRS ${project_path} 
                        INCLUDE_DIRS ${project_path})

# target_compile_options(
#     ${COMPONENT_LIB}
#     PRIVATE -Wno-error=unused-const-variable -Wno-error=delete-non-virtual-dtor
#             -Wno-error=implicit-function-declaration -fpermissive)