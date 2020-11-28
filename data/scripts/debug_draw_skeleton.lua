function onGUI()
    local render_scene = Lumix.main_universe:getScene("renderer")
    local model = this.model_instance:getModel()
    --local model = Renderer.getModelInstanceModel(render_scene._scene, this)
    local bone_count = model:getBoneCount()
    for i = 0, bone_count - 1 do 
        local pos = Renderer.getPoseBonePosition(render_scene._scene, this, i)
        --local pos = Model.getBonePosition(model, i)
        local parent = Model.getBoneParent(model._value, i)
        if parent >= 0 then
            --local parent_pos = Model.getBonePosition(model, parent)
            local parent_pos = Renderer.getPoseBonePosition(render_scene._scene, this, parent)
            render_scene:addDebugLine(pos, parent_pos, 0xff0000ff)
        end
    end
end
