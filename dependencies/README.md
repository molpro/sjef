# Workflow
**To set up git hooks run `git config core.hooksPath git-hooks`**


## Build Type: **All**

##### Initial Build:
- Fetches the dependencies.

##### Pull:
- If stored commit is modified, dependence is reloaded

##### Commit:
- *Ensure that `hooksPath` is set to git-hooks*
- Commit will fail, if dependencies are not in a well defined state i.e.
   * uncommitted changes
   * stored hash different from current hashes
- Fix each dependence before committing
   * commit any changes or reset
   * update stored hash (run `update_SHA1.sh`)

##### Pull Dependencies:
- For each dependence
 1. Checkout a branch
 2. `git pull`
- Run `update_SHA1.sh`
- Afterwards, repeat by running `pull.sh`
   * requires dependence to be on a branch

##### Working on a dependence:
- Treat each dependence as a seperate Project
   * build it independently from the Main Project
- After committing, run `update_SHA1.sh`


## Build type: **Release**

##### Reloading CMakeLists.txt:
- Checks out commit stored in `${dependence}_SHA1`.
- **Raises an error, if `git checkout` fails**
   * make sure dependencies are not modfified, and stored hash is up to date


## Build type: **Debug** or **Development**

##### Reloading CMakeLists.txt:
- Warns if dependencies are modified or have the wrong hashes
   * does not raise errors
- Up to the user to make sure dependencies are in correct state
- Before committing main project
   * commit modified dependence
   * update stored hash (`update_SHA1.sh`)


# Directory Structure

###### `${dependence}/`
- Directory of a loaded dependence

###### `${dependence}_SHA1`
- File with commit hash


# Scripts

###### `check_commit.sh`
- Checks that all dependencies are in a well defined state.
- This script is run before committing the main repository via a git-hook.
If a dependence has uncommitted changes or its hash is different from the one
stored, than an error is returned.

###### `checkout.sh ${dependence} [${build_type}]`
- Updates the dependence by checking out commit stored in `${dependence}_SHA1`.
This is used by FetchContent at the Update stage.
- If `${build_type}` == Development or Debug, than dependence is left unchanged.
- Otherwise, tries to check out the stored commit and raises error if it cannot.

###### `checkout_all.sh [${build_type}]`
- Runs checkout.sh on each dependence

###### `update_SHA1.sh`
- Updates hashes stored in `${dependence}_SHA1` with the current hash number.

###### `pull.sh`
- Pulls each dependency and updates hashes
