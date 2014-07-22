Param (
    $variables = @{},   
    $artifacts = @{},
    $scriptPath,
    $buildFolder,
    $srcFolder,
    $outFolder,
    $tempFolder,
    $projectName,
    $projectVersion,
    $projectBuildNumber
)

foreach($artifact in $artifacts.values)
{
	& ncftpput -u $variables["username"] -p $variables["password"] $variables["server"] $variables["server_path"] $($artifact.path)
}