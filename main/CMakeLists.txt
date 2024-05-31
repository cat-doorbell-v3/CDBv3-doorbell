# idf_component_register(SRCS "app_main.cpp")
idf_component_register(
   SRCS 
      "app_main.cpp" 
   REQUIRES 
      config_manager 
      wifi_manager
      http_manager
   PRIV_REQUIRES 
      modules
)
