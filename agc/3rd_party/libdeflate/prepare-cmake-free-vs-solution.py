#!/usr/bin/env python3
import subprocess
import sys
import os
import fileinput
import shutil

# I write this script to easier reproduce the procedure for new dependencies but also if it is necessary for some other purposes (i.e. there is new VS version or something like that)
# It's just always better to have things as reproducible as possible
# If this is to be used in other project it should be revised (for example here also a directory `programs` is created by cmake)
if os.name != 'nt':
	print("This script is for windows only")
	sys.exit(1)
        
def run_cmd(cmd):
    p = subprocess.Popen(cmd, shell=True)
    p.communicate()

def replace_in_file(file_path, search_text, new_text):
    with fileinput.input(file_path, inplace=True) as file:
        for line in file:
            new_line = line.replace(search_text, new_text)
            print(new_line, end='')

build_dir="build-vs"

run_cmd(f"mkdir {build_dir}")
absolute_path_parent = os.getcwd()[0].upper() + os.getcwd()[1:] # make the dirve letter capital
os.chdir(build_dir)

absolute_path = os.getcwd()[0].upper() + os.getcwd()[1:] # make the dirve letter capital

print("absolute_path:", absolute_path)
print("absolute_path_parent:", absolute_path_parent)

run_cmd(f"cmake ..")

shutil.rmtree("CmakeFiles")
os.remove("CMakeCache.txt")
os.remove("cmake_install.cmake")
os.remove("libdeflate-config-version.cmake")
os.remove("libdeflate-config.cmake")
os.remove("libdeflate-targets.cmake")
shutil.rmtree("programs/CmakeFiles")
os.remove("programs/cmake_install.cmake")

# remove INSTALL.vcxproj because it constains some cmake post build which I think are not needed
os.remove("INSTALL.vcxproj")
os.remove("INSTALL.vcxproj.filters")
os.remove("programs/INSTALL.vcxproj")
os.remove("programs/INSTALL.vcxproj.filters")

def fix_sln(path):
	with open(sln) as f:
		lines = f.readlines()

		is_removing = False
		out = []
		for line in lines:
			if is_removing:
				if "EndProject" in line:
					is_removing = False
			elif "INSTALL.vcxproj" in line:
				is_removing = True
			else:
				out.append(line)

	with open(sln, "w") as f:
		f.writelines(out)

# remove CustomBuild using CMakeFiles.txt and other cmake related stuff
def remove_empty_item_group(l):
	out = []
	is_removing = False
	for i in range(0, len(l) - 1):
		if is_removing:
			is_removing = False
		elif "<ItemGroup>" in l[i] and "</ItemGroup>" in l[i+1]:
			is_removing = True
		else:
			out.append(l[i])

	out.append(l[-1])
	return out

def remove_cmake_from_vcxproj(path):
	with open(path) as f:
		lines = f.readlines()
		is_removing = False

		out = []
		for line in lines:
			if is_removing:
				if "</CustomBuild" in line:
					is_removing = False
			elif "<CustomBuild" in line and "CMake" in line:
				is_removing = True
			else:
				out.append(line)
	
	#remove empty itemgroup
	out = remove_empty_item_group(out)
	
	with open(path, "w") as f:
		f.writelines(out)


def remove_cmake_from_vcxproj_filters(path):
	with open(path) as f:
		lines = f.readlines()
		
		out = []
		is_removing = False
		for line in lines:
			if is_removing:
				if "</CustomBuild" in line or "</Filter" in line:
					is_removing = False
			elif "<CustomBuild" in line and "CMake" in line and "/>" in line:
				pass
			elif ("<CustomBuild" in line or "<Filter" in line ) and "CMake" in line:
				is_removing = True
			else:
				out.append(line)

	out = remove_empty_item_group(out)
	
	with open(path, "w") as f:
		f.writelines(out)

dirs = [".", "programs"]
vcxproj_paths = []
vcxproj_filters_paths = []
slns = []
for dir in dirs:
	for a in os.listdir(dir):
		path = os.path.join(dir, a)
		if a.endswith(".sln"):
			slns.append(path)
		if a.endswith(".vcxproj"):
			vcxproj_paths.append(path)
		if a.endswith(".vcxproj.filters"):
			vcxproj_filters_paths.append(path)

for sln in slns:
	print("Fixing", sln)
	fix_sln(sln)

for vcxproj_path in vcxproj_paths:
	print("Fixing", vcxproj_path)
	remove_cmake_from_vcxproj(vcxproj_path)

	#ugly workaround
	new_path = "./"
	new_path_parent = ".."

	if vcxproj_path.startswith("programs"):
		new_path = ".."
		new_path_parent = "../.."

	replace_in_file(vcxproj_path, absolute_path, new_path)
	replace_in_file(vcxproj_path, absolute_path_parent, new_path_parent)

	replace_in_file(vcxproj_path, absolute_path.replace("\\", "/"), new_path)
	replace_in_file(vcxproj_path, absolute_path_parent.replace("\\", "/"), new_path_parent)
	

for vcxproj_filters_path in vcxproj_filters_paths:
	print("Fixing", vcxproj_filters_path)
	remove_cmake_from_vcxproj_filters(vcxproj_filters_path)

	#ugly workaround
	new_path = "./"
	new_path_parent = ".."

	if vcxproj_path.startswith("programs"):
		new_path = ".."
		new_path_parent = "../.."

	replace_in_file(vcxproj_filters_path, absolute_path, new_path)
	replace_in_file(vcxproj_filters_path, absolute_path_parent, new_path_parent)

	replace_in_file(vcxproj_filters_path, absolute_path.replace("\\", "/"), new_path)
	replace_in_file(vcxproj_filters_path, absolute_path_parent.replace("\\", "/"), new_path_parent)

