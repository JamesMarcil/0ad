/* eslint-disable prefer-const -- Mods should be able to change it */
let g_IncompatibleModsFile = "gui/incompatible_mods/incompatible_mods.txt";
/* eslint-enable prefer-const */

export function init(data)
{
	Engine.GetGUIObjectByName("mainText").caption = Engine.TranslateLines(Engine.ReadFile(g_IncompatibleModsFile));
	return new Promise(closePageCallback =>
	{
		Engine.GetGUIObjectByName("btnClose").onPress = closePageCallback;
	});
}
