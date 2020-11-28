local enabled = {}

function onGUI()
    local render_scene = Lumix.main_universe:getScene("renderer")
    local model = this.model_instance:getModel()
    --local model = Renderer.getModelInstanceModel(render_scene._scene, this)
    local bone_count = model:getBoneCount()
    local deselect_all = false
    if ImGui.Button("Deselect all") then
        deselect_all = true
    end
    for i = 0, bone_count - 1 do 
        local pos = Renderer.getPoseBonePosition(render_scene._scene, this, i)
        --local pos = Model.getBonePosition(model, i)
        local parent = model:getBoneParent(i)
        if parent >= 0 then
            --local parent_pos = Model.getBonePosition(model, parent)
            local parent_pos = Renderer.getPoseBonePosition(render_scene._scene, this, parent)
            local bone_name = model:getBoneName(i)

            local e = true
            if enabled[bone_name] ~= nil then 
                e = enabled[bone_name]
            end
            local changed, e = ImGui.Checkbox(bone_name, e)
            if changed then
                enabled[bone_name] = e
            end
            if deselect_all then
                enabled[bone_name] = false
            end
            if e then
                render_scene:addDebugLine(pos, parent_pos, 0xff0000ff)
            end
        end
    end
end
