export async function init()
{
	return { [Engine.openRequest]: {
		"page": "page_modmod.xml",
		"argument": { "cancelbutton": false }
	} };
}
