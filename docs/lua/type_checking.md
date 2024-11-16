# Type checking 


1. Download and install [Luau language server](https://marketplace.visualstudio.com/items?itemName=JohnnyMorganz.luau-lsp) for VS Code.
2. Open `data` folder in VS Code.
3. Create VS Code workspace settings (`data/.vscode/settings.json`).
4. Put following json there:
```js
{
	"luau-lsp.types.definitionFiles": ["scripts/lumix.d.lua"],
}
```
5. Create `.luaurc` in `data\scripts\` and put following json there:
```js
{
	"languageMode": "nonstrict",
	"lint": { "*": true, "FunctionUnused": false },
	"lintErrors": true,
	"globals": ["expect"]
}
```
6. Typechecking should now work, you can test with following script:
```lua
function onInputEvent(event : InputEvent)
	return event.some_prop
end
```

