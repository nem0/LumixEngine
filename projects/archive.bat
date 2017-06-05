mkdir ..\..\lumixengine_data_exported\
copy .itch.toml ..\..\lumixengine_data_exported
cd ..\..\lumixengine_data
"C:/Program Files/Git/bin/git.exe" archive master | "C:/Program Files/Git/usr/bin/tar.exe" -x -C "../lumixengine_data_exported/"