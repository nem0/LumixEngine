<!-- :: Batch section
@echo off
setlocal

start mshta.exe %0
goto :EOF
-->


<HTML>
<HEAD>
<!--<HTA:APPLICATION SCROLL="no" SYSMENU="no" >-->
<HTA:APPLICATION SCROLL="no">

<TITLE>Install</TITLE>
<SCRIPT language="JavaScript">
window.resizeTo(250,300);
var fso = new ActiveXObject("Scripting.FileSystemObject");
var app = new ActiveXObject("Shell.Application");

function archive()
{
	app.ShellExecute("archive.bat")
}

function symstore()
{
	app.ShellExecute("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/symstore.exe", "add /s \"../../lumixengine_pdb/\" /compress /r /f ../../lumixengine_data/bin/*.pdb /t LumixEngine", "../../lumixengine_data/bin/")
	setTimeout(function() {
		app.ShellExecute("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/symstore.exe", "add /s \"../../lumixengine_pdb/\" /compress /r /f ../../lumixengine_data/bin/*.exe /t LumixEngine", "../../lumixengine_data/bin/")	
	}, 2000)
}

function publishToItchIO()
{
	app.ShellExecute("C:/Users/Miki/AppData/Roaming/itch/bin/butler.exe", "login")
	setTimeout(function() {
		app.ShellExecute("C:/Users/Miki/AppData/Roaming/itch/bin/butler.exe", "push ../../lumixengine_data_exported mikulasflorek/lumix-engine:win-64")
	}, 2000)
}

function install()
{
	var dest_dir = "../../lumixengine_data/bin/"
	var src_dir = "./tmp/vs2017/bin/relwithdebinfo/"
	fso.CopyFile(src_dir + "studio.exe", dest_dir)
	fso.CopyFile(src_dir + "app.exe", dest_dir)
	fso.CopyFile(src_dir + "studio.pdb", dest_dir)
	fso.CopyFile(src_dir + "app.exe", dest_dir)
}

function generateProject()
{
	app.ShellExecute("genie_static_vs17.bat")
}

function updateTests()
{
	app.ShellExecute("genie.exe", "update-tests")
}

function runRenderTests()
{
	app.ShellExecute("genie.exe", "run-render-tests")
}

function openInVS()
{
	app.ShellExecute("C:/Program Files (x86)/Microsoft Visual Studio/2017/Community/Common7/IDE/devenv.exe", "tmp/vs2017/LumixEngine.sln")
}

function build(configuration)
{
	app.ShellExecute("C:/Program Files (x86)/Microsoft Visual Studio/2017/Community/MSBuild/15.0/Bin/msbuild.exe", "tmp/vs2017/LumixEngine.sln /p:Configuration=" + configuration + " /p:Platform=x64")
}

function cleanAll()
{
	if(fso.FolderExists("tmp")) fso.DeleteFolder("tmp");
}


</SCRIPT>
</HEAD>
<BODY>
   <button style="width:200" onclick="generateProject();">Generate VS project</button>
   <button style="width:200" onclick="build('relwithdebinfo');">Build</button>
   <button style="width:200" onclick="install();">Install</button>
   <button style="width:200" onclick="archive();">Archive</button>
   <button style="width:200" onclick="publishToItchIO();">Publish to itch.io</button>
   <button style="width:200" onclick="openInVS();">Open in VS</button>
   <button style="width:200" onclick="symstore();">Symstore</button>
   <button style="width:200" onclick="updateTests();">Update tests</button>
   <button style="width:200" onclick="runRenderTests();">Run render tests</button>
   <button style="width:200" onclick="cleanAll();">Clean all</button>
</BODY>
</HTML>