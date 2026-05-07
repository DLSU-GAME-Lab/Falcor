add_plugin(WaterPass)

target_sources(WaterPass PRIVATE
    WaterPass.cpp
    WaterPass.h
)

target_source_group(WaterPass "RenderPasses")
