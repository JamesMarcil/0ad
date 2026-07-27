export function downloadModsButton(initMods)
{
	initTerms({
		"Disclaimer": {
			"title": translate("Disclaimer"),
			"file": "gui/modio/Disclaimer.txt",
			"config": "modio.disclaimer",
			"accepted": false,
			"callback": openModIo.bind(undefined, initMods),
			"urlButtons": [
				{
					"caption": translate("mod.io Terms"),
					"url": "https://mod.io/terms"
				},
				{
					"caption": translate("mod.io Privacy Policy"),
					"url": "https://mod.io/privacy"
				}
			]
		}
	});

	openTerms("Disclaimer");
}

async function openModIo(initMods, data)
{
	if (!data.accepted)
		return;

	await Engine.OpenChildPage("page_modio.xml");
	initMods();
}
