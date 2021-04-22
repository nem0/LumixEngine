download_project()
{
	if [ ! -d "3rdparty" ]; then
        mkdir 3rdparty
    fi
	cd 3rdparty
	if [ ! -d $1 ]; then
	    git clone --depth=1 $2 $1
    else
		cd $1
	    git pull
		cd ..
    fi
	cd ..
}

build_recast()
{
    ./genie --file=recastnavigation.lua gmake
    pushd 3rdparty/recast/_project
    make 
    popd
}

build_luajit()
{
    pushd 3rdparty/luajit/src
    make
    popd
}

build_physx()
{
    pushd 3rdparty/physx/physx
    sh generate_projects.sh lumix_gcc5
    cd compiler/linux-release
    make
    popd
}

build_freetype() 
{
    pushd 3rdparty/freetype
    jam
    popd
}

pause()
{
    read -p "Press enter to continue"
}

build_3rdparty()
{
    case "$1" in
        "freetype" ) build_freetype;;
        "luajit" ) build_luajit;;
        "physx" ) build_physx;;
        "recast" ) build_recast;;
    esac
}

deploy_freetype()
{
    mkdir -p ../external/freetype/lib/linux64_gmake/release
    cp 3rdparty/freetype/objs/libfreetype.a ../external/freetype/lib/linux64_gmake/release/libfreetype.a
}

deploy_luajit()
{
    mkdir -p ../external/luajit/lib/linux64_gmake/release
    cp 3rdparty/luajit/src/libluajit.a ../external/luajit/lib/linux64_gmake/release/libluajit.a
}

deploy_recast()
{
    mkdir -p ../external/recast/lib/linux64_gmake/release
    cp 3rdparty/recast/_build/libRecast.a ../external/recast/lib/linux64_gmake/release/librecast.a
}

deploy_physx()
{
    mkdir -p ../external/physx/lib/linux64_gmake/release
    cp 3rdparty/physx/physx/bin/linux.clang/release/*.a ../external/physx/lib/linux64_gmake/release/
}

deploy_3rdparty()
{
    case "$1" in
        "freetype" ) deploy_freetype;;
        "luajit" ) deploy_luajit;;
        "physx" ) deploy_physx;;
        "recast" ) deploy_recast;;
    esac
}

project_menu()
{
    while :; do
        clear;
        echo $1
        options=("download", "build", "deploy", "back")
        select opt in "${options[@]}"
        do
            case "$REPLY" in
                1 ) download_project $1 $2; pause; break;;
                2 ) build_3rdparty $1; pause; break;;
                3 ) deploy_3rdparty $1; pause; break;;
                4 ) break 2;;
            esac
        done
    done
}

init_3rdparty()
{
    download_project "freetype" "https://github.com/nem0/freetype2.git"
    download_project "luajit" "https://github.com/nem0/LuaJIT.git"
    download_project "physx" "https://github.com/nem0/PhysX.git"
    download_project "recast" "https://github.com/nem0/recastnavigation.git"

    build_3rdparty "freetype"
    build_3rdparty "luajit"
    build_3rdparty "physx"
    build_3rdparty "recast"

    deploy_3rdparty "freetype"
    deploy_3rdparty "luajit"
    deploy_3rdparty "physx"
    deploy_3rdparty "recast"

    pause;
}

thirdparty_menu()
{
    while :; do
        clear;
        echo "third party"
        options=("freetype" "luajit" "physx" "recast" "init all" "back")
        select opt in "${options[@]}"
        do
            case "$REPLY" in
                1 ) project_menu "freetype" "https://github.com/nem0/freetype2.git"; break;;
                2 ) project_menu "luajit" "https://github.com/nem0/LuaJIT.git"; break;;
                3 ) project_menu "physx" "https://github.com/nem0/PhysX.git"; break;;
                4 ) project_menu "recast" "https://github.com/nem0/recastnavigation.git"; break;;
                5 ) init_3rdparty; break;;
                6 ) break 2;;
            esac
        done
    done
}

push_to_itch_io()
{
    git clean -f -x -d ../data/
    rm -rf itch_io
    mkdir itch_io
    ./genie gmake
    cd tmp/gmake 
    make -j config=relwithdebinfo64
    cd ../..
    cp tmp/gmake/bin/RelWithDebInfo/studio itch_io/studio
    chmod +x itch_io/studio
    cp -r ../data/* itch_io/
    cp .itch.toml itch_io/

    if [ ! -f butler ]; then
        curl -L -o butler.zip https://broth.itch.ovh/butler/linux-amd64/LATEST/archive/default
        unzip butler.zip
        rm butler.zip
        chmod +x butler
    fi

    ./butler push itch_io mikulasflorek/lumix-engine:linux
}

build()
{
    cd tmp/gmake
    make -j config=$1
    cd ../..
}

function pause(){
 read -s -n 1 -p "Press any key to continue . . ."
 echo ""
}

main_menu()
{
    clear;
    PS3="Wut? "
    options=("create project" "build release" "build debug" "3rdparty", "push to itch.io", "exit")
    select opt in "${options[@]}"
    do
        case "$REPLY" in
            1 ) ./genie gmake; pause; break;;
            2 ) build relwithdebinfo64; pause; break;;
            3 ) build debug64; pause; break;;
            4 ) thirdparty_menu; break;;
            5 ) push_to_itch_io; break;;
            6 ) exit;;
        esac
    done
    pause
}

while :; do main_menu; done
